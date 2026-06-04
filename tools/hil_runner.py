#!/usr/bin/env python3
"""
HIL 오케스트레이션 — HIL-001 테스트케이스를 실보드에 자동 실행(자극→관측→판정).

관측(Observe):
  - CAN heartbeat: 0x100(Drive)/0x201(Sensor), data[0]=fw_version, data[1]=active_slot
  - ECU UART 디버그 로그: [BL]/[DRIVE]/[SensorECU] 라인
자극(Stimulus):
  - OTA 푸시: tools/ota_client.py (subprocess)
  - 메타 위조 주입: tools/forge_meta.py + st-flash
  - 전원/센서/버튼 등 물리 동작: 운영자 프롬프트(input)

하드웨어 없이 파싱 로직만 검증:  python hil_runner.py --selftest
실보드 실행:                     python hil_runner.py --all \
    --can slcan0 --drive-uart /dev/tty.drive --sensor-uart /dev/tty.sensor \
    --key keys/ota_priv.pem --img fixtures/

자극 중 일부(전원 재인가·센서 분리·버튼)는 자동화 불가 → 프롬프트로 운영자에게 지시한다.
"""

import argparse
import subprocess
import sys
import threading
import time
from collections import deque

# ── 순수 파싱(하드웨어 불필요, --selftest로 검증) ──────────────────────────────
HEARTBEAT = {0x100: "drive", 0x201: "sensor"}


def decode_heartbeat(can_id, data):
    """heartbeat 프레임 → (ecu, version, slot) 또는 None. data[0]=ver, data[1]=slot."""
    ecu = HEARTBEAT.get(can_id)
    if ecu is None or len(data) < 2:
        return None
    return (ecu, data[0], data[1])


class LogBuffer:
    """스레드-세이프 라인 버퍼 + 패턴 대기. SerialLog가 채우고, 테스트가 wait_for로 읽는다."""

    def __init__(self):
        self._lines = deque(maxlen=4000)
        self._lock = threading.Lock()

    def feed(self, line):
        with self._lock:
            self._lines.append((time.time(), line))

    def _snapshot(self):
        with self._lock:
            return list(self._lines)

    def saw(self, pattern, since=0.0):
        return any(pattern in ln for ts, ln in self._snapshot() if ts >= since)

    def wait_for(self, pattern, timeout, since=None):
        """timeout 내에 pattern을 포함한 라인이 나오면 True. since 이후 라인만 검사."""
        since = time.time() if since is None else since
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.saw(pattern, since):
                return True
            time.sleep(0.02)
        return False


# ── 하드웨어 인터페이스(지연 import) ─────────────────────────────────────────
class SerialLog(LogBuffer):
    def __init__(self, port, baud=115200):
        super().__init__()
        import serial  # lazy
        self._ser = serial.Serial(port, baud, timeout=0.2)
        self._stop = False
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        buf = b""
        while not self._stop:
            try:
                buf += self._ser.read(256)
            except Exception:
                break
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                self.feed(raw.decode("utf-8", "replace").rstrip("\r"))

    def close(self):
        self._stop = True


class CanHeartbeat:
    def __init__(self, channel, interface="slcan", bitrate=500000):
        import can  # lazy
        self._bus = can.interface.Bus(channel=channel, interface=interface, bitrate=bitrate)
        self._latest = {}        # ecu -> (version, slot, ts)
        self._lock = threading.Lock()
        self._stop = False
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while not self._stop:
            msg = self._bus.recv(timeout=0.5)
            if msg is None:
                continue
            hb = decode_heartbeat(msg.arbitration_id, msg.data)
            if hb:
                ecu, ver, slot = hb
                with self._lock:
                    self._latest[ecu] = (ver, slot, time.time())

    def latest(self, ecu):
        with self._lock:
            return self._latest.get(ecu)

    def wait_version(self, ecu, version, slot=None, timeout=20.0):
        """timeout 내 해당 ECU heartbeat가 version(+slot)으로 관측되면 True."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            v = self.latest(ecu)
            if v and v[0] == version and (slot is None or v[1] == slot):
                return True
            time.sleep(0.05)
        return False

    def close(self):
        self._stop = True


# ── 자극 ────────────────────────────────────────────────────────────────────
def sh(cmd):
    print(f"   $ {' '.join(cmd)}")
    return subprocess.run(cmd, capture_output=True, text=True)


def ota_push(cfg, ecu, image):
    return sh([sys.executable, "tools/ota_client.py", "--ecu", ecu,
               "--channel", cfg.can, "--interface", cfg.interface,
               "--bitrate", str(cfg.bitrate), image])


def forge_inject(cfg, preset, addr="0x08008000"):
    out = "/tmp/hil_meta.bin"
    sh([sys.executable, "tools/forge_meta.py", "--preset", preset, "--out", out])
    r1 = sh(["st-flash", "--reset", "write", out, addr])
    r2 = sh(["st-flash", "--reset", "write", out, "0x0800C000"])
    return r1.returncode == 0 and r2.returncode == 0


def st_reset():
    return sh(["st-flash", "reset"]).returncode == 0


def manual(prompt):
    input(f"   [수동] {prompt} — 완료 후 Enter...")


# ── 테스트 케이스 ────────────────────────────────────────────────────────────
class Result:
    def __init__(self, tc, passed, detail=""):
        self.tc, self.passed, self.detail = tc, passed, detail


def tc01_anti_rollback(cfg, drv, can):
    """전제: CONFIRMED v2 실행 중. v1(옛) 푸시 → 거부 + v2 복귀."""
    since = time.time()
    ota_push(cfg, "drive", f"{cfg.img}/drive_v1_signed.bin")
    refused = drv.wait_for("anti-rollback:", 30, since) and drv.wait_for("refusing", 30, since)
    recovered = can.wait_version("drive", 2, timeout=30)
    never_v1 = not (can.latest("drive") and can.latest("drive")[0] == 1)
    ok = refused and recovered and never_v1
    return Result("TC-01 anti-rollback", ok,
                  f"refused={refused} v2복귀={recovered} v1미부팅={never_v1}")


def tc02_three_strike(cfg, drv, can):
    """전제: CONFIRMED v2. 고장 v3 푸시 → 3-strike 후 v2 롤백(~30s)."""
    since = time.time()
    ota_push(cfg, "drive", f"{cfg.img}/drive_v3_broken_signed.bin")
    trial = drv.wait_for("trial start:", 30, since)
    strike = drv.wait_for("3-strike:", 60, since)
    recovered = can.wait_version("drive", 2, slot=0, timeout=60)
    ok = trial and strike and recovered
    return Result("TC-02 3-strike rollback", ok,
                  f"trial={trial} 3-strike={strike} v2/slotA복귀={recovered}")


def tc03_fail_closed(cfg, drv, can):
    """size=0 위조 메타 주입 → 부트로더 fail-closed 거부, 앱 미부팅."""
    since = time.time()
    forge_inject(cfg, "size0-attack")
    st_reset()
    refused = drv.wait_for("fail-closed", 20, since)
    no_boot = not can.wait_version("drive", 2, timeout=8)   # heartbeat 안 나와야 PASS
    ok = refused and no_boot
    return Result("TC-03 fail-closed (size=0)", ok,
                  f"fail-closed로그={refused} 앱미부팅={no_boot}")


def tc04_staleness(cfg, drv, can):
    """주행 중 센서 분리 → ~150ms 내 fail-safe 정지."""
    manual("Drive를 출발시켜 전진 상태로 만드세요(B1), 모터가 도는지 확인")
    since = time.time()
    manual("이제 SensorECU 전원/CAN을 분리하세요(0x200 중단)")
    stale = drv.wait_for("fail-safe", 3, since)   # "[DRIVE] 센서 stale → fail-safe 정지"
    return Result("TC-04 sensor staleness", stale, f"fail-safe정지={stale}")


ALL_TCS = {
    "01": tc01_anti_rollback,
    "02": tc02_three_strike,
    "03": tc03_fail_closed,
    "04": tc04_staleness,
}


# ── 셀프테스트(하드웨어 불필요) ───────────────────────────────────────────────
def selftest():
    fails = []

    def check(name, cond):
        print(f"  {'OK ' if cond else 'FAIL'}  {name}")
        if not cond:
            fails.append(name)

    # heartbeat 디코드
    check("decode 0x100 -> (drive,2,0)", decode_heartbeat(0x100, bytes([2, 0, 0])) == ("drive", 2, 0))
    check("decode 0x201 -> (sensor,3,1)", decode_heartbeat(0x201, bytes([3, 1])) == ("sensor", 3, 1))
    check("decode unknown id -> None", decode_heartbeat(0x123, bytes([1, 1])) is None)
    check("decode short data -> None", decode_heartbeat(0x100, bytes([1])) is None)

    # 로그 매처
    lb = LogBuffer()
    t0 = time.time()
    for ln in ["[BL] ECDSA OK",
               "[BL] anti-rollback: v1 below baseline — refusing",
               "[BL] rollback to slot 1 (CONFIRMED) + reset"]:
        lb.feed(ln)
    check("wait_for 존재 패턴", lb.wait_for("anti-rollback:", 0.2, since=t0))
    check("wait_for 부재 패턴 → False", lb.wait_for("fail-closed", 0.1, since=t0) is False)
    check("saw rollback", lb.saw("rollback to slot"))

    print("\nSELFTEST:", "ALL PASS" if not fails else f"{len(fails)} FAIL")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description="HIL-001 오케스트레이션")
    ap.add_argument("--selftest", action="store_true", help="하드웨어 없이 파싱 로직 검증")
    ap.add_argument("--tc", choices=ALL_TCS.keys(), help="단일 TC 실행")
    ap.add_argument("--all", action="store_true", help="전체 TC 실행")
    ap.add_argument("--can", default="slcan0"); ap.add_argument("--interface", default="slcan")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--drive-uart"); ap.add_argument("--sensor-uart")
    ap.add_argument("--key", default="keys/ota_priv.pem")
    ap.add_argument("--img", default="fixtures", help="서명 이미지 디렉토리")
    cfg = ap.parse_args()

    if cfg.selftest:
        return selftest()

    if not cfg.drive_uart:
        ap.error("실보드 실행엔 --drive-uart 필요(또는 --selftest)")

    drv = SerialLog(cfg.drive_uart)
    can = CanHeartbeat(cfg.can, cfg.interface, cfg.bitrate)
    tcs = ALL_TCS.values() if cfg.all else [ALL_TCS[cfg.tc]]
    results = []
    try:
        for fn in tcs:
            print(f"\n=== {fn.__doc__.splitlines()[0].strip()} ===")
            results.append(fn(cfg, drv, can))
    finally:
        drv.close(); can.close()

    print("\n" + "=" * 60)
    for r in results:
        print(f"  [{'PASS' if r.passed else 'FAIL'}] {r.tc:32} {r.detail}")
    return 0 if all(r.passed for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
