#!/usr/bin/env python3
"""
UDS OTA client for STM32 DriveECU firmware upload via CAN / ISO-TP.

Usage (macOS / slcan):
    python ota_client.py --channel /dev/tty.usbmodemXXXX firmware_slotb.bin

Usage (RPi5 / socketcan):
    python ota_client.py --channel can0 --interface socketcan firmware_slotb.bin

Security: Key = HMAC-SHA256(PSK, Seed)[0:4]  (must match uds.c)
          PSK from env OTA_PSK_HEX (64 hex chars), else the dev placeholder.
"""

import argparse
import hashlib
import hmac
import os
import struct
import time
import sys
import can

ECU_IDS = {
    'drive':  {'tx': 0x7E0, 'rx': 0x7E8, 'hb_id': 0x100, 'driving_byte': 2},
    'sensor': {'tx': 0x7E1, 'rx': 0x7E9, 'hb_id': None,  'driving_byte': None},
}
# PreSharedKey for SecurityAccess. DEV PLACEHOLDER — must match the PSK in the
# Bootloader's WRP region (Bootloader/Core/Src/psk.c). Override via env OTA_PSK_HEX.
_DEV_PSK  = b"OTA-DEV-PSK-DO-NOT-USE-IN-PROD!!"
PSK       = bytes.fromhex(os.environ["OTA_PSK_HEX"]) if os.environ.get("OTA_PSK_HEX") else _DEV_PSK
CHUNK     = 256    # data bytes per TransferData
# Target slot address is determined by the ECU (inactive slot auto-selected)


class ISOTPError(Exception):
    pass


class UDSError(Exception):
    pass


class OTAClient:
    def __init__(self, bus: can.BusABC, ecu: str = 'drive', cf_delay: float = 0.005):
        self.bus      = bus
        self.ecu      = ecu
        self.ISOTP_TX = ECU_IDS[ecu]['tx']
        self.ISOTP_RX = ECU_IDS[ecu]['rx']
        self.cf_delay = cf_delay

    # ------------------------------------------------------- idle detection --

    def wait_for_idle(self, timeout: float = 120.0) -> None:
        """CAN heartbeat를 모니터링하여 ECU가 IDLE(주행 중 아님) 상태가 될 때까지 대기."""
        hb_id        = ECU_IDS[self.ecu]['hb_id']
        driving_byte = ECU_IDS[self.ecu]['driving_byte']
        if hb_id is None:
            return  # SensorECU는 주행 상태 없음 — 즉시 진행

        print(f"[OTA] ECU IDLE 대기 중 (최대 {timeout:.0f}초)...")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            msg = self.bus.recv(timeout=1.0)
            if msg is None or msg.arbitration_id != hb_id:
                continue
            if len(msg.data) > driving_byte and msg.data[driving_byte] == 0:
                print("[OTA] ECU IDLE 확인 → OTA 시작")
                return
            remaining = int(deadline - time.monotonic())
            print(f"\r[OTA] 주행 중 — 대기... ({remaining}초 남음)  ", end="", flush=True)
        print()
        raise ISOTPError(f"ECU IDLE 대기 타임아웃 ({timeout:.0f}초) — OTA 중단")

    # ------------------------------------------------------------------ send --

    def _send_sf(self, data: bytes) -> None:
        assert len(data) <= 7
        frame = bytes([len(data)]) + data + bytes(7 - len(data))
        self.bus.send(can.Message(arbitration_id=self.ISOTP_TX, data=frame,
                                  is_extended_id=False))

    def _send_ff(self, data: bytes) -> None:
        total = len(data)
        ff = bytes([0x10 | (total >> 8), total & 0xFF]) + data[:6]
        self.bus.send(can.Message(arbitration_id=self.ISOTP_TX, data=ff,
                                  is_extended_id=False))

    def _send_cf(self, chunk: bytes, sn: int) -> None:
        frame = bytes([0x20 | (sn & 0x0F)]) + chunk + bytes(7 - len(chunk))
        self.bus.send(can.Message(arbitration_id=self.ISOTP_TX, data=frame,
                                  is_extended_id=False))

    def _send_fc(self) -> None:
        self.bus.send(can.Message(arbitration_id=self.ISOTP_TX,
                                  data=bytes([0x30, 0, 0, 0, 0, 0, 0, 0]),
                                  is_extended_id=False))

    # ------------------------------------------------------------------ recv --

    def _recv_msg(self, timeout: float = 5.0) -> bytes:
        """Receive one complete ISO-TP message from ECU."""
        deadline = time.monotonic() + timeout
        buf = b""
        total = 0
        received = 0
        next_sn = 1

        while time.monotonic() < deadline:
            msg = self.bus.recv(timeout=min(0.5, deadline - time.monotonic()))
            if not msg or msg.arbitration_id != self.ISOTP_RX:
                continue
            d = bytes(msg.data)
            pci = d[0] & 0xF0

            if pci == 0x00:                     # Single Frame
                n = d[0] & 0x0F
                return d[1:1 + n]

            elif pci == 0x10:                   # First Frame
                total = ((d[0] & 0x0F) << 8) | d[1]
                buf = d[2:8]
                received = 6
                self._send_fc()

            elif pci == 0x20:                   # Consecutive Frame
                sn = d[0] & 0x0F
                if sn != next_sn:
                    raise ISOTPError(f"SN mismatch: got {sn}, expected {next_sn}")
                next_sn = (next_sn + 1) & 0x0F
                remaining = total - received
                n = min(7, remaining)
                buf += d[1:1 + n]
                received += n
                if received >= total:
                    return buf

        raise ISOTPError("Receive timeout")

    def _isotp_send(self, data: bytes) -> None:
        if len(data) <= 7:
            self._send_sf(data)
            return
        self._send_ff(data)
        # Wait for Flow Control
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            msg = self.bus.recv(timeout=0.5)
            if msg and msg.arbitration_id == self.ISOTP_RX and (msg.data[0] & 0xF0) == 0x30:
                break
        else:
            raise ISOTPError("No Flow Control from ECU")
        sn = 1
        offset = 6
        while offset < len(data):
            self._send_cf(data[offset:offset + 7], sn)
            sn = (sn + 1) & 0x0F
            offset += 7
            time.sleep(self.cf_delay)

    def request(self, data: bytes, timeout: float = 5.0) -> bytes:
        self._isotp_send(data)
        resp = self._recv_msg(timeout)
        if resp[0] == 0x7F:
            raise UDSError(f"NRC SID=0x{resp[1]:02X} code=0x{resp[2]:02X}")
        return resp

    # -------------------------------------------------------- UDS services --

    def session_extended(self) -> None:
        print("[UDS] DiagnosticSessionControl(Extended)")
        r = self.request(bytes([0x10, 0x02]))
        if r[0] != 0x50 or r[1] != 0x02:
            raise UDSError(f"Unexpected response: {r.hex()}")

    def security_access(self) -> None:
        print("[UDS] SecurityAccess - seed request")
        r = self.request(bytes([0x27, 0x01]))
        if r[0] != 0x67 or r[1] != 0x01:
            raise UDSError(f"Unexpected response: {r.hex()}")
        seed = r[2:6]   # 4-byte Seed (HMAC message, big-endian as received)
        print(f"[UDS]   Seed = {seed.hex()}")

        key = hmac.new(PSK, seed, hashlib.sha256).digest()[:4]
        print(f"[UDS]   Key  = {key.hex()}")
        r = self.request(bytes([0x27, 0x02]) + key)
        if r[0] != 0x67 or r[1] != 0x02:
            raise UDSError(f"Unexpected response: {r.hex()}")
        print("[UDS] Unlocked")

    def request_download(self, size: int) -> int:
        print(f"[UDS] RequestDownload  size={size} bytes  (ECU selects target slot)")
        payload = (bytes([0x34, 0x00, 0x44])
                   + struct.pack(">I", 0)        # addr=0: ECU auto-selects inactive slot
                   + struct.pack(">I", size))
        # Erase takes ~4 seconds — use extended timeout
        r = self.request(payload, timeout=15.0)
        if r[0] != 0x74:
            raise UDSError(f"Unexpected response: {r.hex()}")
        ml_bytes = (r[1] >> 4) & 0x0F
        max_block = int.from_bytes(r[2:2 + ml_bytes], "big")
        max_data  = max_block - 1          # subtract blockSeq byte
        print(f"[UDS]   maxBlockLen={max_block}  data per chunk={max_data}")
        return max_data

    def transfer_data(self, fw: bytes, max_data: int) -> None:
        total  = len(fw)
        offset = 0
        bsq    = 1
        n_chunks = (total + max_data - 1) // max_data
        print(f"[UDS] TransferData: {n_chunks} chunks x {max_data} bytes")
        while offset < total:
            chunk = fw[offset:offset + max_data]
            pad   = (4 - len(chunk) % 4) % 4      # pad last chunk to 4B align
            chunk = chunk + bytes([0xFF] * pad)
            r = self.request(bytes([0x36, bsq]) + chunk, timeout=5.0)
            if r[0] != 0x76 or r[1] != bsq:
                raise UDSError(f"Block {bsq} unexpected response: {r.hex()}")
            offset += max_data
            pct = min(100, offset * 100 // total)
            print(f"\r[UDS]   {pct:3d}%  block={bsq}", end="", flush=True)
            bsq = 0x00 if bsq == 0xFF else bsq + 1
        print()

    def transfer_exit(self) -> None:
        print("[UDS] RequestTransferExit")
        r = self.request(bytes([0x37]))
        if r[0] != 0x77:
            raise UDSError(f"Unexpected response: {r.hex()}")
        print("[UDS] Transfer complete — ECU will reboot to new slot")


# --------------------------------------------------------------------------- #

def main() -> None:
    parser = argparse.ArgumentParser(description="STM32 ECU OTA client")
    parser.add_argument("--ecu", default="drive", choices=["drive", "sensor"],
                        help="Target ECU (drive=0x7E0/7E8, sensor=0x7E1/7E9)")
    parser.add_argument("--channel", required=True,
                        help="slcan: /dev/tty.usbmodemXXXX  |  socketcan: can0")
    parser.add_argument("--interface", default="slcan",
                        choices=["slcan", "socketcan"],
                        help="CAN interface type (default: slcan)")
    parser.add_argument("--bitrate", type=int, default=500000)
    parser.add_argument("--idle-timeout", type=float, default=120.0,
                        help="ECU IDLE 대기 최대 시간(초). 0이면 즉시 진행 (default: 120)")
    parser.add_argument("--cf-delay", type=float, default=0.005,
                        help="ISO-TP Consecutive Frame 전송 간격(초) (default: 0.005)")
    parser.add_argument("--declared-size", type=int, default=None,
                        help="RequestDownload에 선언할 size(공격용). 실제 파일보다 작게 주면 "
                             "endless-data 시험 — 누적 초과 시 ECU가 NRC 0x31로 거부해야 정상")
    parser.add_argument("firmware", help="Signed firmware .bin file")
    args = parser.parse_args()

    with open(args.firmware, "rb") as f:
        fw = f.read()
    print(f"[OTA] ECU={args.ecu}  Firmware={args.firmware}  ({len(fw)} bytes)")

    if args.interface == "socketcan":
        bus = can.interface.Bus(interface="socketcan", channel=args.channel)
    else:
        bus = can.interface.Bus(interface="slcan",
                                channel=args.channel,
                                bitrate=args.bitrate)
    try:
        client = OTAClient(bus, ecu=args.ecu, cf_delay=args.cf_delay)
        if args.idle_timeout > 0:
            client.wait_for_idle(timeout=args.idle_timeout)
        client.session_extended()
        client.security_access()
        declared = args.declared_size if args.declared_size is not None else len(fw)
        max_data = client.request_download(declared)
        max_data = min(max_data, CHUNK)
        try:
            client.transfer_data(fw, max_data)
        except UDSError as e:
            if args.declared_size is not None and len(fw) > declared:
                print(f"\n[ATTACK] endless-data: ECU가 선언 size({declared}B) 초과 전송을 "
                      f"거부함 (기대된 결과) — {e}")
                return
            raise
        client.transfer_exit()
    except (UDSError, ISOTPError) as e:
        print(f"\n[OTA] ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()
