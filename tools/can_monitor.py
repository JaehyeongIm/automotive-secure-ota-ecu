"""
CAN bus monitor for DriveECU (0x100) and SensorECU (0x201).
Usage: python can_monitor.py [--channel slcan0] [--bitrate 500000]
"""

import argparse
import time
import can

# CAN ID 정의 (SRS Section 13.2)
CAN_ID_DRIVE_STATUS   = 0x100
CAN_ID_SENSOR_HB      = 0x201

def parse_drive_status(data: bytes) -> str:
    if len(data) < 8:
        return "invalid length"
    counter = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7]
    return f"payload={data[:4].hex()} cnt={counter}"

def parse_sensor_heartbeat(data: bytes) -> str:
    if len(data) < 5:
        return "invalid length"
    alive = data[0]
    hb = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4]
    return f"alive=0x{alive:02X} hb={hb}"

PARSERS = {
    CAN_ID_DRIVE_STATUS: ("DriveECU  ", parse_drive_status),
    CAN_ID_SENSOR_HB:    ("SensorECU ", parse_sensor_heartbeat),
}

def monitor(channel: str, bitrate: int) -> None:
    bus = can.interface.Bus(channel=channel, interface="slcan", bitrate=bitrate)
    print(f"Listening on {channel} @ {bitrate} bps  (Ctrl+C to quit)\n")
    print(f"{'Timestamp':>12}  {'ID':>6}  {'Source':<10}  {'Info'}")
    print("-" * 70)

    try:
        while True:
            msg = bus.recv(timeout=1.0)
            if msg is None:
                continue
            ts = time.strftime("%H:%M:%S", time.localtime(msg.timestamp))
            ms = int((msg.timestamp % 1) * 1000)
            name, parser = PARSERS.get(msg.arbitration_id, (None, None))
            if parser:
                info = parser(msg.data)
                print(f"{ts}.{ms:03d}  0x{msg.arbitration_id:03X}  {name}  {info}")
            else:
                print(f"{ts}.{ms:03d}  0x{msg.arbitration_id:03X}  {'Unknown':<10}  {msg.data.hex()}")
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        bus.shutdown()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--channel", default="slcan0", help="CAN interface (default: slcan0)")
    parser.add_argument("--bitrate", type=int, default=500000, help="Bit rate (default: 500000)")
    args = parser.parse_args()
    monitor(args.channel, args.bitrate)
