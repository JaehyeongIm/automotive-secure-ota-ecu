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


SILENCE_THRESH = 0.5   # heartbeat 없으면 리셋 중으로 판단 (초)
SILENCE_WAIT   = 3.0   # Phase 1 최대 대기 (초)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--ecu', choices=['drive', 'sensor'], required=True)
    parser.add_argument('--channel', default='can0')
    parser.add_argument('--timeout', type=float, default=45.0)
    parser.add_argument('--wait-reboot', action='store_true',
                        help='재부팅 완료 후 슬롯 확인 (2-phase: 소멸 → 복구)')
    args = parser.parse_args()

    target_id = CAN_ID[args.ecu]

    with can.interface.Bus(interface='socketcan', channel=args.channel) as bus:
        if args.wait_reboot:
            _wait_reboot(bus, target_id, args.timeout)
        else:
            _read_once(bus, target_id, args.timeout, args.ecu, args.channel)


def _read_once(bus, target_id, timeout, ecu, channel):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if msg and msg.arbitration_id == target_id and len(msg.data) >= 2:
            print('A' if msg.data[1] == 0 else 'B')
            return
    print(f'TIMEOUT: no heartbeat from {ecu} ECU on {channel}', file=sys.stderr)
    sys.exit(1)


def _wait_reboot(bus, target_id, timeout):
    """Phase 1: heartbeat 소멸 대기 → Phase 2: 재부팅 후 슬롯 확인."""
    t_start  = time.monotonic()
    deadline = t_start + timeout

    # Phase 1: ECU 리셋 확인 (heartbeat 침묵)
    last_hb    = t_start
    silence_dl = t_start + SILENCE_WAIT
    while time.monotonic() < min(deadline, silence_dl):
        msg = bus.recv(timeout=0.2)
        if msg and msg.arbitration_id == target_id:
            last_hb = time.monotonic()
        if time.monotonic() - last_hb > SILENCE_THRESH:
            break

    # Phase 2: 재부팅 완료 후 새 슬롯 확인
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if msg and msg.arbitration_id == target_id and len(msg.data) >= 2:
            print('A' if msg.data[1] == 0 else 'B')
            return

    print('TIMEOUT: ECU did not reboot within timeout', file=sys.stderr)
    sys.exit(1)


if __name__ == '__main__':
    main()
