#!/usr/bin/env python3
"""
UDS OTA client for STM32 DriveECU firmware upload via CAN / ISO-TP.

Usage:
    python ota_client.py --channel /dev/tty.usbmodemXXXX firmware_slotb.bin

Security: key = seed XOR 0xDEADBEEF  (must match SEC_MASK in uds.c)
"""

import argparse
import struct
import time
import sys
import can

ISOTP_TX = 0x7E0   # PC → ECU
ISOTP_RX = 0x7E8   # ECU → PC
SEC_MASK  = 0xDEADBEEF
SLOT_B    = 0x08040000
CHUNK     = 256    # data bytes per TransferData


class ISOTPError(Exception):
    pass


class UDSError(Exception):
    pass


class OTAClient:
    def __init__(self, bus: can.BusABC):
        self.bus = bus

    # ------------------------------------------------------------------ send --

    def _send_sf(self, data: bytes) -> None:
        assert len(data) <= 7
        frame = bytes([len(data)]) + data + bytes(7 - len(data))
        self.bus.send(can.Message(arbitration_id=ISOTP_TX, data=frame,
                                  is_extended_id=False))

    def _send_ff(self, data: bytes) -> None:
        total = len(data)
        ff = bytes([0x10 | (total >> 8), total & 0xFF]) + data[:6]
        self.bus.send(can.Message(arbitration_id=ISOTP_TX, data=ff,
                                  is_extended_id=False))

    def _send_cf(self, chunk: bytes, sn: int) -> None:
        frame = bytes([0x20 | (sn & 0x0F)]) + chunk + bytes(7 - len(chunk))
        self.bus.send(can.Message(arbitration_id=ISOTP_TX, data=frame,
                                  is_extended_id=False))

    def _send_fc(self) -> None:
        self.bus.send(can.Message(arbitration_id=ISOTP_TX,
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
            if not msg or msg.arbitration_id != ISOTP_RX:
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
            if msg and msg.arbitration_id == ISOTP_RX and (msg.data[0] & 0xF0) == 0x30:
                break
        else:
            raise ISOTPError("No Flow Control from ECU")
        sn = 1
        offset = 6
        while offset < len(data):
            self._send_cf(data[offset:offset + 7], sn)
            sn = (sn + 1) & 0x0F
            offset += 7
            time.sleep(0.001)

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
        seed = struct.unpack(">I", r[2:6])[0]
        print(f"[UDS]   Seed = 0x{seed:08X}")

        key = seed ^ SEC_MASK
        print(f"[UDS]   Key  = 0x{key:08X}")
        r = self.request(bytes([0x27, 0x02]) + struct.pack(">I", key))
        if r[0] != 0x67 or r[1] != 0x02:
            raise UDSError(f"Unexpected response: {r.hex()}")
        print("[UDS] Unlocked")

    def request_download(self, addr: int, size: int) -> int:
        print(f"[UDS] RequestDownload  addr=0x{addr:08X}  size={size} bytes")
        payload = (bytes([0x34, 0x00, 0x44])
                   + struct.pack(">I", addr)
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
        print("[UDS] Transfer complete — ECU will reboot to Slot B")


# --------------------------------------------------------------------------- #

def main() -> None:
    parser = argparse.ArgumentParser(description="STM32 DriveECU OTA client")
    parser.add_argument("--channel", required=True,
                        help="Serial port for CANable (e.g. /dev/tty.usbmodemXXXX)")
    parser.add_argument("--bitrate", type=int, default=500000)
    parser.add_argument("firmware", help="Slot B firmware .bin file")
    args = parser.parse_args()

    with open(args.firmware, "rb") as f:
        fw = f.read()
    print(f"[OTA] Firmware: {args.firmware}  ({len(fw)} bytes)")

    bus = can.interface.Bus(interface="slcan",
                            channel=args.channel,
                            bitrate=args.bitrate)
    try:
        client = OTAClient(bus)
        client.session_extended()
        client.security_access()
        max_data = client.request_download(SLOT_B, len(fw))
        max_data = min(max_data, CHUNK)
        client.transfer_data(fw, max_data)
        client.transfer_exit()
    except (UDSError, ISOTPError) as e:
        print(f"\n[OTA] ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()
