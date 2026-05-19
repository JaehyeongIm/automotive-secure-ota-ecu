# 하드웨어 독립적 선행 구현 기록

하드웨어 부품 미도착 또는 배선 이슈로 물리적 통합 테스트가 불가능한 상황에서,
소프트웨어 계층을 먼저 완성해 개발 공백을 제거한 과정을 기록합니다.

---

## 1. Phase 1 → Phase 2 전환 (USB 케이블 미도착)

### 상황
Phase 1(CAN 버스 하드웨어 배선)을 진행하려 했으나 보드에 전원을 공급할 USB 케이블이 미도착.
물리적 배선 자체가 불가능한 상태.

### 선택
하드웨어 없이 구현 가능한 **PC 측 CAN 모니터** 를 먼저 작성.

### 구현 내용

**`tools/can_monitor.py`**
- python-can 라이브러리 + slcan 인터페이스로 CANable 연결
- 0x100(DriveECU), 0x201(SensorECU) 프레임 파싱 및 출력
- 이후 USB 케이블 도착 후 실제 하드웨어에 바로 적용해 정상 동작 확인

```
하드웨어 없이 완성한 것:
  - CAN 프레임 파싱 로직
  - slcan 인터페이스 연결 방식
  - 실시간 모니터링 출력 포맷

부품 도착 후 추가한 것:
  - 없음 (그대로 사용)
```

---

## 2. Phase 3 Bootloader (Flash 레이아웃 기반 선행 설계)

### 상황
Bootloader를 설계하려면 Flash 메모리 구조를 먼저 확정해야 하고,
STM32 보드가 있어도 플래싱 전에 소프트웨어 로직을 완성할 수 있는 구간.

### 선택
Flash 레이아웃을 먼저 설계하고, 하드웨어 없이 검증 가능한 모든 로직을 완성.

### 구현 내용

**Flash 레이아웃 확정 (STM32F446RE 512KB 기준)**

```
0x08000000  Sector 0-1  Bootloader   (32KB)
0x08008000  Sector 2    Metadata     (16KB)
0x08010000  Sector 4-5  Slot A App   (192KB)  ← DriveECU 현재 위치
0x08040000  Sector 6-7  Slot B App   (256KB)  ← OTA 업로드 대상
```

**`Bootloader/Core/Inc/bootloader.h`**
- `OTA_Metadata_t` 구조체 정의
- 매직 넘버, 슬롯 상태 상수 (CONFIRMED / PENDING / INVALID)
- Slot A/B 주소 상수

**`Bootloader/Core/Src/bootloader.c`**
- 슬롯 선택 로직 (active_slot 기준 + 유효성 검사 + fallback)
- `is_valid_app()`: SP 주소로 유효한 앱 여부 판별
- `jump_to_app()`: VTOR 설정 → MSP 설정 → Reset Handler 점프
- `safe_state()`: 모든 슬롯 invalid 시 IWDG 킥하며 대기
- IWDG 초기화 (앱이 8초 내 kick 안 하면 리셋)

**`DriveECU/STM32F446RETX_FLASH.ld`**
- ORIGIN = 0x8010000, LENGTH = 192K 로 수정 (Slot A 위치)

**`DriveECU/Core/Src/system_stm32f4xx.c`**
- VECT_TAB_OFFSET = 0x00010000U 로 수정

```
하드웨어 없이 완성한 것:
  - Flash 레이아웃 설계
  - 슬롯 선택 알고리즘 전체
  - 점프 메커니즘 (ARM Cortex-M4 방식)
  - Metadata 구조체

부품 도착/플래싱 후 확인한 것:
  - [BL] Jump to 0x08010000 → [DriveECU] Start 정상 동작
  - IWDG 8초 동작 확인
```

---

## 3. Phase 4 UDS/ISO-TP/OTA 스택 (CAN 배선 이슈 중 선행 구현)

### 상황
DriveECU CAN 버스 단절 이슈(Bus-Off 추정)로 실제 OTA 전송 테스트가 불가능.
하지만 프로토콜 로직 자체는 하드웨어와 무관하게 완성 가능.

### 선택
STM32 측 UDS/ISO-TP 스택과 PC Python OTA 클라이언트를 동시에 완성.
CAN 물리 계층 이슈 해결 후 바로 통합 테스트 진입 가능한 상태로 준비.

### 구현 내용

**STM32 측 (DriveECU)**

| 파일 | 역할 |
|---|---|
| `isotp.c/h` | ISO-TP SF/FF/CF/FC 처리, CAN 인터럽트에서 수신 후 콜백 호출 |
| `uds.c/h` | UDS 서비스 핸들러 (0x10/0x27/0x34/0x36/0x37) + 상태 머신 |
| `ota_flash.c/h` | Slot B Sector 6-7 소거, 4바이트 정렬 쓰기, Metadata 기록 |

**모듈 설계 원칙**
- `isotp.c`: CAN 하드웨어와 직접 통신, 완성 메시지를 콜백으로 전달
- `uds.c`: isotp 위에서 동작, Flash 쓰기는 ota_flash에 위임
- `ota_flash.c`: HAL Flash API만 사용, UDS 로직과 완전 분리

→ 세 모듈이 독립적으로 설계되어 단위 테스트 및 교체 용이

**PC 측 (`tools/ota_client.py`)**

```
① session_extended()   : 0x10 02 → 0x50 02
② security_access()    : 시드 요청 → key = seed XOR 0xDEADBEEF → 인증
③ request_download()   : 0x34 + Slot B 주소 + 펌웨어 크기 → Flash 소거
④ transfer_data()      : 256바이트 청크 × N회 → Flash 쓰기
⑤ transfer_exit()      : 0x37 → Metadata 기록 → NVIC_SystemReset
```

**`DriveECU/STM32F446RETX_FLASH_SlotB.ld`**
- ORIGIN = 0x8040000, LENGTH = 256K (Slot B 바이너리용 링커 스크립트)

```
하드웨어 없이 완성한 것:
  - ISO-TP 레이어 전체 (SF/FF/CF/FC)
  - UDS 상태 머신 및 서비스 핸들러
  - Flash 소거/쓰기/Metadata 기록 로직
  - Python OTA 클라이언트 전체
  - Slot B 링커 스크립트

CAN 배선 이슈 해결 후 검증할 것:
  - E2E OTA 전송 → Slot B 부팅 → [DriveECU v2] Start 확인
```

---

## 4. 현재 상태 요약

| Phase | 소프트웨어 구현 | 하드웨어 검증 |
|---|---|---|
| Phase 1 CAN 통신 | ✅ 완료 | ✅ 완료 |
| Phase 2 메시지 구조 | ✅ 완료 | ✅ 완료 |
| Phase 3 Bootloader | ✅ 완료 | ✅ 완료 |
| Phase 4 UDS/OTA | ✅ 완료 | ⏳ CAN 배선 이슈 해결 후 진행 |
| Phase 5 ECDSA/Jenkins | 미시작 | 미시작 |
| Phase 6 RPi5/RC카 | 미시작 | ⏳ 부품 배송 대기 |

---

## 핵심 설계 원칙

하드웨어 독립적 선행 구현이 가능했던 이유:

1. **계층 분리**: 물리 계층(CAN 하드웨어) — 프로토콜 계층(ISO-TP) — 애플리케이션 계층(UDS) 을 명확히 분리
2. **HAL 경계 활용**: STM32 HAL API를 인터페이스 경계로 설정해 하드웨어 의존성을 한 곳으로 격리
3. **PC 측 쌍 구현**: STM32 수신 로직과 Python 송신 클라이언트를 동시에 작성해 양쪽 로직을 맞춰가며 검증
