#!/usr/bin/env python3
"""
Read active slot from ECU CAN heartbeat.

DriveECU  0x100 data[1]: active_slot (0=A, 1=B)
SensorECU 0x201 data[1]: active_slot (0=A, 1=B)

Usage:
    python3 ci/read_slot.py --ecu drive
    python3 ci/read_slot.py --ecu sensor
Prints: A or B
Exit 1 on timeout.
"""

import argparse
import sys
import time

import can

CAN_ID = {'drive': 0x100, 'sensor': 0x201}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--ecu', choices=['drive', 'sensor'], required=True)
    parser.add_argument('--channel', default='can0')
    parser.add_argument('--timeout', type=float, default=10.0)
    args = parser.parse_args()

    target_id = CAN_ID[args.ecu]
    deadline = time.monotonic() + args.timeout

    with can.interface.Bus(interface='socketcan', channel=args.channel) as bus:
        while time.monotonic() < deadline:
            msg = bus.recv(timeout=1.0)
            if msg and msg.arbitration_id == target_id and len(msg.data) >= 2:
                slot = msg.data[1]
                print('A' if slot == 0 else 'B')
                return

    print(f'TIMEOUT: no heartbeat from {args.ecu} ECU on {args.channel}', file=sys.stderr)
    sys.exit(1)


if __name__ == '__main__':
    main()
