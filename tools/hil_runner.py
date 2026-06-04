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

하드웨어 없이 파싱 로직만 검증:  python3 hil_runner.py --selftest

[권장] Mac에서 전부 오케스트레이션 (ST-Link·UART=로컬, OTA=ngrok→Pi 게이트웨이):
    python3 hil_runner.py --ecu drive --build --setup --all --uart /dev/tty.usbmodemXXX \
        --ota-cmd "curl -s -F fw=@{img} https://YOURID.ngrok.io/ota/{ecu}"
  관측=UART(로컬), 플래시=로컬 ST-Link, OTA 자극=--ota-cmd(운영 경로 재사용) → CAN 불필요.

[단일 머신] 모든 장치가 한 곳에:
    python3 hil_runner.py --ecu drive --build --setup --all --can slcan0 --uart /dev/ttyUSB0

  --build=픽스처 빌드, --setup=부트로더+베이스라인 플래시. 단일 단계: --setup --tc 03 등.

자극 중 일부(전원 재인가·센서 분리·버튼)는 자동화 불가 → 프롬프트로 운영자에게 지시한다.
"""

import argparse
import os
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
                line = raw.decode("utf-8", "replace").rstrip("\r")
                self.feed(line)
                if line:                       # ECU UART 로그를 터미널에 실시간 echo
                    print(f"   uart| {line}")

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
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:        # 실패는 반드시 노출(st-flash 충돌·보드 미인식 등)
        out = (r.stdout + r.stderr).strip()
        print(f"   ! exit={r.returncode}")
        if out:
            print("   " + out.replace("\n", "\n   "))
    return r


def stflash(cfg, *args):
    """st-flash 호출. --st-serial 주면 해당 ST-Link만 타깃(보드 2개 동시연결 대응)."""
    cmd = ["st-flash"]
    if cfg.st_serial:
        cmd += ["--serial", cfg.st_serial]
    return sh(cmd + list(args))


def ota_push(cfg, ecu, image):
    # 운영 경로(ngrok→Pi 게이트웨이 등) 재사용: {img},{ecu} 치환 후 실행
    if cfg.ota_cmd:
        cmd_str = cfg.ota_cmd.format(img=image, ecu=ecu)
        print(f"   $ {cmd_str}")
        return subprocess.run(cmd_str, shell=True, capture_output=True, text=True)
    cmd = [sys.executable, "tools/ota_client.py", "--ecu", ecu,
           "--channel", cfg.can or "slcan0", "--interface", cfg.interface,
           "--bitrate", str(cfg.bitrate), image]
    if cfg.ota_remote:            # CAN이 다른 머신(Pi)에 있을 때: Pi에서 수동 푸시
        manual("Pi에서 OTA 푸시 실행:\n        " + " ".join(cmd) +
               f"\n        (이미지 {image} 가 Pi에 있어야 함 — 미리 scp)")
        return None
    return sh(cmd)


def forge_inject(cfg, preset, addr="0x08008000"):
    out = "/tmp/hil_meta.bin"
    sh([sys.executable, "tools/forge_meta.py", "--preset", preset, "--out", out])
    r1 = stflash(cfg, "--reset", "write", out, addr)
    r2 = stflash(cfg, "--reset", "write", out, "0x0800C000")
    return r1.returncode == 0 and r2.returncode == 0


def st_reset(cfg):
    return stflash(cfg, "reset").returncode == 0


def manual(prompt):
    input(f"   [수동] {prompt} — 완료 후 Enter...")


def setup_bench(cfg):
    """known-state 셋업: 부트로더 + 베이스라인 v2(Slot A CONFIRMED) 플래시(멱등)."""
    print("=== 벤치 셋업: 부트로더 + 베이스라인 v2 (Slot A CONFIRMED) ===")
    sh(["make", "-C", "Bootloader/Debug", "-j4", "all"])
    sh(["arm-none-eabi-objcopy", "-O", "binary",
        "Bootloader/Debug/Bootloader.elf", "/tmp/hil_bl.bin"])
    stflash(cfg, "--reset", "write", "/tmp/hil_bl.bin", "0x08000000")
    base = f"{cfg.img}/{cfg.ecu}_v2_A.bin"
    stflash(cfg, "--reset", "write", base, "0x08010000")
    sz = os.path.getsize(base)
    sh([sys.executable, "tools/forge_meta.py", "--preset", "confirmed-a",
        "--a-version", "2", "--a-size", str(sz), "--out", "/tmp/hil_meta_ok.bin"])
    stflash(cfg, "--reset", "write", "/tmp/hil_meta_ok.bin", "0x08008000")
    stflash(cfg, "--reset", "write", "/tmp/hil_meta_ok.bin", "0x0800C000")
    st_reset(cfg)


# ── 테스트 케이스 ────────────────────────────────────────────────────────────
class Result:
    def __init__(self, tc, passed, detail=""):
        self.tc, self.passed, self.detail = tc, passed, detail


def tc01_anti_rollback(cfg, drv, can):
    """전제: CONFIRMED v2 실행 중. v1(옛) 푸시 → 거부 + v2 복귀(UART로 판정)."""
    e = cfg.ecu
    since = time.time()
    ota_push(cfg, e, f"{cfg.img}/{e}_v1_B.bin")
    refused = drv.wait_for("anti-rollback:", 40, since)
    recovered = drv.wait_for("version v2 OK", 40, since)       # 롤백 후 v2 부팅
    hb_ok = (can is None) or can.wait_version(e, 2, timeout=20)
    ok = refused and recovered and hb_ok
    return Result("TC-01 anti-rollback", ok,
                  f"거부={refused} v2복귀={recovered} heartbeat={hb_ok}")


def tc02_three_strike(cfg, drv, can):
    """전제: CONFIRMED v2. 고장 v3 푸시 → 3-strike 후 v2 롤백(UART, ~30s)."""
    e = cfg.ecu
    since = time.time()
    ota_push(cfg, e, f"{cfg.img}/{e}_v3broken_B.bin")
    trial = drv.wait_for("trial start:", 40, since)
    strike = drv.wait_for("3-strike:", 80, since)
    recovered = drv.wait_for("version v2 OK", 90, since)
    ok = trial and strike and recovered
    return Result("TC-02 3-strike rollback", ok,
                  f"trial={trial} 3-strike={strike} v2복귀={recovered}")


def tc03_fail_closed(cfg, drv, can):
    """size=0 위조 메타 주입(ST-Link) → 부트로더 fail-closed 거부(UART로 판정)."""
    since = time.time()
    forge_inject(cfg, "size0-attack")
    st_reset(cfg)
    refused = drv.wait_for("fail-closed", 20, since)
    return Result("TC-03 fail-closed (size=0)", refused, f"fail-closed로그={refused}")


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
    ap.add_argument("--ecu", default="drive", choices=["drive", "sensor"], help="시험 대상 ECU")
    ap.add_argument("--build", action="store_true", help="픽스처 빌드(build_fixtures.sh) 먼저")
    ap.add_argument("--setup", action="store_true", help="부트로더+베이스라인 플래시(known-state)")
    ap.add_argument("--tc", choices=ALL_TCS.keys(), help="단일 TC 실행")
    ap.add_argument("--all", action="store_true", help="전체 TC 실행")
    ap.add_argument("--can", default=None, help="CAN 채널(없으면 heartbeat 생략, UART로 판정)")
    ap.add_argument("--interface", default="slcan")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--ota-remote", action="store_true",
                    help="OTA 푸시를 프롬프트로(다른 머신서 수동 실행)")
    ap.add_argument("--ota-cmd",
                    help="OTA 푸시 명령 템플릿({img},{ecu} 치환). "
                         "예: 'curl -F fw=@{img} https://X.ngrok.io/ota/{ecu}'")
    ap.add_argument("--uart", help="시험 대상 ECU 디버그 UART 포트")
    ap.add_argument("--st-serial", help="ST-Link 시리얼(보드 2개 동시연결 시 타깃 지정; st-info --probe로 확인)")
    ap.add_argument("--key", default="ota-private-key.pem")
    ap.add_argument("--img", default="fixtures", help="서명 이미지 디렉토리")
    cfg = ap.parse_args()

    if cfg.selftest:
        return selftest()

    if cfg.build:
        subprocess.run(["bash", "tools/build_fixtures.sh", cfg.ecu], check=True)

    if not (cfg.all or cfg.tc):
        return 0   # 빌드만 했으면 종료

    if not cfg.uart:
        ap.error("실보드 실행엔 --uart 필요(또는 --selftest)")

    drv = SerialLog(cfg.uart)
    can = CanHeartbeat(cfg.can, cfg.interface, cfg.bitrate) if cfg.can else None
    results = []
    try:
        if cfg.setup:
            setup_bench(cfg)
        tcs = ALL_TCS.values() if cfg.all else [ALL_TCS[cfg.tc]]
        for fn in tcs:
            print(f"\n=== {fn.__doc__.splitlines()[0].strip()} ===")
            results.append(fn(cfg, drv, can))
    finally:
        drv.close()
        if can:
            can.close()

    print("\n" + "=" * 60)
    for r in results:
        print(f"  [{'PASS' if r.passed else 'FAIL'}] {r.tc:32} {r.detail}")
    return 0 if all(r.passed for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
