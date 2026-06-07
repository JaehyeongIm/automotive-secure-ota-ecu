# SIT-001 RUNBOOK — On-Target 공격 시나리오 테스트 실행 절차

> [SIT-001 테스트 플랜](SIT-001_System_Integration_Test_Plan.md)의 TC를 실보드에서 차근차근 실행하는 **운영 가이드**.
> 역할: **Mac** = 오케스트레이션(hil_runner)·플래시(ST-Link)·UART 관측 / **Pi** = CAN 버스·OTA 자극.
> 판정은 hil_runner가 ECU UART 로그를 자동 대조해 `[PASS]/[FAIL]`로 출력한다.

---

## 0. 이 벤치 고정값

| 항목 | 값 |
|---|---|
| Drive ST-Link 시리얼 | `0669FF485775495067211743` (끝 …43) |
| Sensor ST-Link 시리얼 | `066EFF485775495067194557` (끝 …57) |
| Drive UART | `/dev/tty.usbmodem21203` |
| Sensor UART | `/dev/tty.usbmodem21303` |
| Pi 접속 | `raspberrypi@192.168.45.215`, CAN = `can0`(socketcan) |
| 서명키 | `ota-private-key.pem` (repo 루트, Mac) |

확인: 시리얼 `st-info --probe` · UART `ls /dev/tty.usbmodem*` · (재연결 시 값이 바뀔 수 있음)

---

## 1. 사전 준비 (세션당 1회)

**1-1. 펌웨어 빌드 + 플래시 (Mac)** — `--build`로 최신 소스 반영
```bash
tools/flash.sh drive  0669FF485775495067211743 --build
tools/flash.sh sensor 066EFF485775495067194557 --build
```
→ 각 보드 UART에 `confirmed boot: slot 0` / `version v2 OK` 떠야 정상.

**1-2. Pi 코드 동기화 (ECU와 같은 커밋)** — 구버전이면 SecurityAccess가 깨짐(ISS-SEC-002)
```bash
ssh raspberrypi@192.168.45.215
cd ~/automotive-secure-ota-ecu
git checkout -- DriveECU/Debug/makefile SensorECU/Debug/Core/Src/subdir.mk \
                SensorECU/Debug/Drivers/STM32F4xx_HAL_Driver/Src/subdir.mk   # 빌드산출물 로컬변경 버림
git pull
echo "$OTA_PSK_HEX"      # 비어 있어야 dev PSK 사용(ECU와 일치). 값 있으면 unset OTA_PSK_HEX
```

**1-3. 공격 픽스처 생성 + Pi로 복사 (Mac)**
```bash
python3 tools/forge_image.py fixtures/drive_v3_B.bin --tamper    # → drive_v3_B_tampered.bin (TC-06)
python3 tools/forge_image.py fixtures/drive_v3_B.bin --unsign    # → drive_v3_B_unsigned.bin (TC-07)
scp fixtures/drive_*.bin raspberrypi@192.168.45.215:~/automotive-secure-ota-ecu/fixtures/
```

**1-4. (TC-08용) Pi can-utils**
```bash
ssh raspberrypi@192.168.45.215 'sudo apt install -y can-utils'
```

---

## 2. 공통 실행 패턴

- **OTA형 TC (01·02·05·06·07):** Mac에서 `hil_runner --tc NN --ota-remote` → "Pi에서 push 실행" 프롬프트가 뜨면 **Pi에서** `ota_client`(아래 명령) 실행 → Mac 터미널에서 **Enter** → 자동 판정.
  - Pi push는 항상 **채널을 `--channel can0 --interface socketcan`** 으로(프롬프트엔 slcan0로 나옴).
  - **전량 전송(35428B)엔 `--cf-delay 0.02`** 필수(ISS-OTA-006). 즉시 거부되는 TC-05는 불필요.
- **Mac 단독 (03):** `--st-serial …43` 필요, Pi 불필요.
- **물리 (04):** 프롬프트 따라 버튼/센서 조작.
- **복구:** `tools/flash.sh drive 0669FF485775495067211743` — TC-03·06·07 뒤 safe_state를 v2/슬롯A로 되돌림.

---

## 3. TC별 실행 (각 TC = 무엇을 검증 + Mac/Pi 명령 + 기대 로그)

### TC-01 — anti-rollback (다운그레이드 거부, FR-BL-008/AB-008)
*서명된 옛 버전(v1)을 밀면 부트로더가 거부하고 v2로 복귀.*
```bash
# Mac
python3 tools/hil_runner.py --ecu drive --tc 01 --uart /dev/tty.usbmodem21203 --ota-remote
# Pi
python3 tools/ota_client.py --ecu drive --channel can0 --interface socketcan --cf-delay 0.02 fixtures/drive_v1_B.bin
```
기대: `anti-rollback: v1 below baseline — refusing` → `rollback to slot 0` → `version v2 OK`. **복구 불필요.**

### TC-02 — 3-strike 롤백 (고장 업데이트 자동 복구, FR-AB-007)
*self-test 실패 펌웨어를 밀면 3회 시험부팅 후 이전 CONFIRMED로 롤백.*
```bash
# Mac
python3 tools/hil_runner.py --ecu drive --tc 02 --uart /dev/tty.usbmodem21203 --ota-remote
# Pi
python3 tools/ota_client.py --ecu drive --channel can0 --interface socketcan --cf-delay 0.02 fixtures/drive_v3broken_B.bin
```
기대: `trial start` → `trial retry`×2 → `3-strike … INVALID, rollback` → `version v2 OK` (~30s). **복구 불필요.**

### TC-03 — fail-closed (size=0 → 검증 우회 차단, FR-AB-003) · **Mac 단독**
*메타 size가 비정상이면 ECDSA를 건너뛰지 않고 부팅 거부.*
```bash
# Mac (Pi 불필요)
python3 tools/hil_runner.py --ecu drive --tc 03 --uart /dev/tty.usbmodem21203 \
    --st-serial 0669FF485775495067211743
```
기대: `metadata size invalid (0x00000000) — fail-closed, refusing` → Safe State.
**복구:** `tools/flash.sh drive 0669FF485775495067211743`

### TC-04 — 센서 staleness fail-safe (ISO 26262 안전상태) · **물리**
*센서가 침묵하면 Drive가 ~150ms 내 정지.*
```bash
# Mac (--ota-remote 불필요)
python3 tools/hil_runner.py --ecu drive --tc 04 --uart /dev/tty.usbmodem21203
```
조작: 프롬프트① **B1로 Drive 출발**(모터 전진 확인) → 프롬프트② **SensorECU 전원/CAN 분리**.
기대: `[DRIVE] 센서 stale → fail-safe 정지`. **복구 불필요**(센서 재연결).

### TC-05 — endless-data (누적 수신 상한, FR-CAN-012)
*작은 size 선언 후 초과 전송 → 거부 + 세션 종료.*
```bash
# Mac
python3 tools/hil_runner.py --ecu drive --tc 05 --uart /dev/tty.usbmodem21203 --ota-remote
# Pi (cf-delay 불필요 — block 2에서 즉시 거부)
python3 tools/ota_client.py --ecu drive --channel can0 --interface socketcan --declared-size 256 fixtures/drive_v3_B.bin
```
기대: `NRC SID=0x36 code=0x31` + 세션종료. **복구 불필요**(메타 미커밋).

### TC-06 — firmware 변조 거부 (TC-ATK-001)
*서명영역 1바이트 변조 → ECDSA 실패 → 부팅 거부.*
```bash
# Mac
python3 tools/hil_runner.py --ecu drive --tc 06 --uart /dev/tty.usbmodem21203 --ota-remote
# Pi
python3 tools/ota_client.py --ecu drive --channel can0 --interface socketcan --cf-delay 0.02 fixtures/drive_v3_B_tampered.bin
```
기대: 전량 전송 → `trial start` → `ECDSA FAILED … refusing` → Safe State.
**복구:** `tools/flash.sh drive 0669FF485775495067211743`

### TC-07 — 미서명/서명무효 거부 (TC-ATK-002)
*서명 64B를 0으로 → REQUIRED 게이트가 ECDSA 실패로 거부(우회 불가).*
```bash
# Mac
python3 tools/hil_runner.py --ecu drive --tc 07 --uart /dev/tty.usbmodem21203 --ota-remote
# Pi
python3 tools/ota_client.py --ecu drive --channel can0 --interface socketcan --cf-delay 0.02 fixtures/drive_v3_B_unsigned.bin
```
기대: `ECDSA FAILED … refusing`. **복구:** `tools/flash.sh drive 0669FF485775495067211743`

### TC-08 — CAN flood 중 OTA: no-brick (TC-ATK-008) · **반자동**
*버스 폭주 중 OTA가 깨져도 brick 없이 이전 펌웨어 유지.*
```bash
# Mac
python3 tools/hil_runner.py --ecu drive --tc 08 --uart /dev/tty.usbmodem21203 --ota-remote
# Pi 터미널1 (프롬프트①에서 시작, 계속 실행)
cangen can0 -g 1 -L 8 -D r
# Pi 터미널2 (프롬프트② push) — 옛 버전(v1)이라 완주해도 anti-rollback으로 v2 귀결
python3 tools/ota_client.py --ecu drive --channel can0 --interface socketcan --cf-delay 0.02 fixtures/drive_v1_B.bin
# 프롬프트③: cangen 중지(Ctrl-C) + 리셋
```
기대: brick 없이 **known-good(v2)로 귀결** — flood로 깨지면 미커밋→v2, 끝까지 가도 anti-rollback→v2. `version v2 OK` 확인.

---

## 4. 복구 / known-state 리셋

언제든 v2/슬롯A(CONFIRMED)로 되돌리기:
```bash
tools/flash.sh drive 0669FF485775495067211743
```
부트로더까지 다시 깔려면 `--bl` 추가. 센서는 `…57` 시리얼.

---

## 5. 트러블슈팅 빠른참조

| 증상 | 원인 | 조치 | 문서 |
|---|---|---|---|
| `NRC SID=0x27 code=0x35` (unlock 실패) | PSK 불일치(Pi 구버전/`OTA_PSK_HEX`) | Pi `git pull` + `unset OTA_PSK_HEX` | ISS-SEC-002 |
| `No Flow Control from ECU` (전송 중단) | RX FIFO 오버런(고부하) | push에 `--cf-delay 0.02` | ISS-OTA-006 |
| `0 KiB flash / Unknown memory region` | ST-Link 글리치 | flash.sh 자동 재시도, 반복 시 USB 재연결 | ISS-HW-001 |
| `ESR=0xFFFE0047` (Bus-Off, CAN 먹통) | 한 노드만 리셋해 desync | 두 보드 동시 리셋(ABOM이 자동복구) | ISS-CAN-006 |
| `FileNotFoundError: fixtures/...` (Pi) | Pi에 픽스처 없음 | Mac에서 `scp fixtures/...` | §1-3 |
| 부팅이 `slot 1`/`v3`(옛 메타) | 메타 B 쓰기 실패로 옛 사본 선택 | `flash.sh` 재실행(재시도 포함) | ISS-HW-001 |

---

## 부록 — fixtures 정체 (서명 이미지)

모두 `[헤더 0x200(OTAH·fw_version·ecu_id)][코드][ECDSA-P256 서명 64B]` 구조(서명됨).

| 파일 | 슬롯 | 헤더 ver | 코드 | 용도 |
|---|---|---|---|---|
| `drive_v2_A.bin` | A | 2 | normal | CONFIRMED 베이스라인(실행 펌웨어) |
| `drive_v1_B.bin` | B | 1 | normal(=v3_B 동일) | TC-01 옛 버전 |
| `drive_v3_B.bin` | B | 3 | normal | TC-05/06/07 원본 |
| `drive_v3broken_B.bin` | B | 3 | self-test-fail | TC-02 고장앱 |
| `drive_v3_B_tampered.bin` | B | 3 | 1바이트 변조(서명 무효) | TC-06 |
| `drive_v3_B_unsigned.bin` | B | 3 | 서명 64B=0 | TC-07 |

`v1_B`와 `v3_B`는 **코드가 동일**하고 헤더 `fw_version`만 다름(anti-rollback 버전은 서명 헤더에 있음). 부팅 로그의 `[DriveECU v2]`는 *소스 하드코딩 표시 문자열*이며 보안 버전은 부트로더의 `version vN OK`다. **TC-01~07은 모든 후보가 거부/롤백되므로 실행 펌웨어는 v2/슬롯A로 불변**(나쁜 업데이트 미적용 — secure OTA의 핵심 속성).
