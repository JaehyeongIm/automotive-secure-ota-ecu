# Troubleshooting Log

## Phase 8 — SlotA OTA 실패 후 반복적 하트비트 없음 (ISS-OTA-005)

**날짜:** 2026-05-27  
**상태:** 원인 분석 완료, 수정 대기

---

### 현상

1. SlotB OTA가 한 번 성공한 이후, SlotA를 대상으로 OTA를 시도한다.
2. SlotA OTA가 **단 한 번이라도 실패**하면, 이후 파이프라인 실행 시 매번 `TIMEOUT: no heartbeat from sensor ECU on can0`가 발생한다.
3. STM32CubeProgrammer로 **메타데이터 섹터(0x08008000)를 소거**하고 SlotA에 펌웨어를 재플래싱하면 복구된다.

```
+ python3 ci/read_slot.py --ecu sensor --channel can0
TIMEOUT: no heartbeat from sensor ECU on can0
ERROR: script returned exit code 1
Finished: FAILURE
```

---

### 시스템 상태 (실패 시점 CubeProgrammer 관측)

```
0x08008000: DEADBEEF  00000000  BBBBBBBB  AAAAAAAA
            [magic]   [active=0][slot_a=PENDING][slot_b=CONFIRMED]

0x08008010: 00000002  00000002  00000000  0000744C
            [a_ver=2] [b_ver=2] [boot_cnt][a_size=29772]

0x08008020: 0000744C  ...
            [b_size=29772]

0x08020000 (SlotA): FFFFFFFF  ← 소거됨
0x08040000 (SlotB): FFFFFFFF  ← 소거됨
```

두 슬롯 모두 소거된 상태에서 메타데이터가 남아 있어, 부트로더가 어느 슬롯도 부팅하지 못하고 `safe_state()`에 진입한다.

---

### 원인 분석

#### 1단계: OTA 성공 직후 정상 상태

SlotB OTA 성공 시 `ota_meta_write_pending(slot=1)` 기록:

```
active_slot    = 1      (SlotB가 활성)
slot_a_status  = CONFIRMED
slot_b_status  = PENDING
slot_b_size    = 29772
```

ECU는 SlotB로 부팅. 하트비트(CAN 0x201)에서 `ota_get_active_slot() = 1` 보고.

---

#### 2단계: SlotA OTA 시도 — 핵심 문제 발생

`RequestDownload` 처리 순서 (`uds.c:137~148`):

```
1. ota_get_active_slot() = 1 → g_target_slot = 0 (SlotA 대상)
2. ota_flash_erase_slot_a()  ← SlotA 플래시 섹터 소거 (0xFF)
3. isotp_send(0x74)          ← 메타데이터 변경 없음
```

**문제**: 소거 전에 메타데이터에 SlotA를 무효 상태로 기록하는 단계가 없다.

소거 직후 상태:

```
실제 플래시:
  SlotA (0x08020000): 0xFF  ← 소거됨
  SlotB (0x08040000): 유효한 펌웨어

메타데이터:
  slot_a_status = CONFIRMED  ← 실제와 불일치 (거짓 주장)
  slot_b_status = PENDING
```

---

#### 3단계: TransferData 실패 시나리오

CF 간격이 짧거나(0.001~0.003s) 다른 원인으로 OTA가 실패하면 `RequestTransferExit(0x37)`에 도달하지 못하고 Python 클라이언트가 종료된다.

- 메타데이터: **변경 없음** (slot_a=CONFIRMED, slot_b=PENDING 유지)
- SlotA: 소거된 상태
- ECU: SlotB에서 계속 실행, 하트비트 유지

---

#### 4단계: ECU 리셋 발생

이후 ECU가 어떤 이유로든 리셋되면(IWDG, 전원, 이후 OTA 시도 중 등):

```
부트로더 판단:
  active_slot=1, slot_b=PENDING → SlotB 부팅 시도
  is_valid_app(SlotB) = true    → 정상 경로
  ECDSA 검증 → 조건부 통과

→ SlotB 정상 부팅 가능  (이 경우 하트비트 있음)
```

그러나 이후 추가 OTA 시도가 이루어지면 상황이 복잡해진다.

---

#### 5단계: RequestTransferExit에서 0x77 응답 소실 → 양쪽 슬롯이 모두 소거되는 경로

CF 간격 0.003~0.005s에서 OTA가 대부분의 블록을 전송하면 `RequestTransferExit(0x37)`에 도달할 수 있다.

```c
// uds.c RequestTransferExit 처리
ota_meta_write_pending(g_target_slot, g_fw_size)  // ← 메타데이터 기록
isotp_send(0x77)     // ← CAN TX 메일박스 포화 시 응답 소실 가능
HAL_Delay(100)
NVIC_SystemReset()   // ← 무조건 리셋
```

0x77 응답이 메일박스 포화로 소실되면:
- OTA 클라이언트: 타임아웃 → "FAILURE" 보고
- ECU: 메타데이터는 정상 기록 + 리셋

이 경우 메타데이터:
```
active_slot   = 0  (SlotA 활성)
slot_a_status = PENDING
slot_b_status = CONFIRMED  ← 실제 SlotB가 유효하다고 가정
```

ECU는 SlotA 부팅을 시도한다. SlotA에 모든 블록이 기록됐으면 정상 부팅 가능.

---

#### 6단계: 양쪽 슬롯 소거 → safe_state()

위 5단계로 SlotA가 활성 슬롯이 된 후, **다음 OTA가 SlotB를 대상으로** 시도되면:

```
ota_get_active_slot() = 0 → g_target_slot = 1 (SlotB 대상)
ota_flash_erase_slot_b()  → SlotB 소거
OTA 실패
```

이제:
- SlotA: 유효 (or 부분 기록)
- SlotB: 소거됨
- 메타데이터: slot_b=CONFIRMED이지만 SlotB는 0xFF

ECU 리셋 시 부트로더:

```
active_slot=0, slot_a=PENDING → SlotA 부팅 시도
is_valid_app(SlotA) → 실패할 경우 (부분 기록이거나 ECDSA 실패)
  → fallback: slot_b_status == CONFIRMED → SlotB 시도
    → is_valid_app(SlotB) = false (0xFF)
      → safe_state() → 하트비트 없음
```

---

### 근본 원인 요약

**플래시 소거와 메타데이터 무효화가 원자적이지 않다.**

| 단계 | 실제 플래시 | 메타데이터 | 문제 |
|------|------------|-----------|------|
| SlotB OTA 성공 | SlotB 유효 | slot_a=CONFIRMED, slot_b=PENDING | 정상 |
| SlotA OTA RequestDownload | **SlotA 소거** | **변경 없음** (slot_a=CONFIRMED 유지) | **불일치 발생** |
| SlotA OTA 실패 | SlotA=0xFF | slot_a=CONFIRMED | 메타데이터가 거짓 주장 |
| 후속 OTA로 SlotB까지 소거 | 양쪽 0xFF | slot_b=CONFIRMED | 부트로더 safe_state() |

OTA 실패가 한 번이라도 발생하면 소거된 슬롯이 CONFIRMED로 잘못 기록된 채로 남고, 이후 연쇄적인 OTA 시도로 두 슬롯이 모두 소거될 수 있다.

---

### 임시 복구 방법

STM32CubeProgrammer에서:

1. 메타데이터 섹터 소거: `0x08008000` (Sector 2, 16KB)
   - magic 없어지면 부트로더가 ECDSA 없이 SlotA 기본 부팅
2. SlotA에 펌웨어 플래싱: `0x08020000` (서명 없는 원본 바이너리)
3. 리셋 → SlotA 정상 부팅 확인

---

### 수정 방향

**`RequestDownload`에서 슬롯 소거 전에 해당 슬롯을 INVALID 상태로 기록해야 한다.**

```
[현재]
  1. 슬롯 소거
  2. (OTA 성공 시만) 메타데이터 기록

[수정안]
  1. 메타데이터에 대상 슬롯을 INVALID로 기록  ← 추가
  2. 슬롯 소거
  3. (OTA 성공 시) 메타데이터에 PENDING으로 기록
```

이렇게 하면 소거 이후 어느 시점에 리셋이 발생해도 부트로더가 해당 슬롯을 신뢰하지 않는다.

---

### 관련 파일

- `SensorECU/Core/Src/uds.c` — `RequestDownload` 처리 (소거 순서)
- `SensorECU/Core/Src/ota_flash.c` — `ota_meta_write_pending()`, `ota_flash_erase_slot_a/b()`
- `Bootloader/Core/Src/bootloader.c` — `bootloader_run()`, `safe_state()`

---

### 관련 문서

- [phase8_sensorECU_ota_random_timeout.md](phase8_sensorECU_ota_random_timeout.md) — OTA 랜덤 타임아웃 (CF 간격 문제)
