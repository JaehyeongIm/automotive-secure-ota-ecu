# Troubleshooting Log

## Phase 5A — RPi5 OTA 중단 후 Bootloader Safe State 진입

**날짜:** 2026-05-20

---

### 현상

RPi5에서 `ota_client.py`로 DriveECU OTA를 시도한 후 DriveECU가 다음 상태가 됐다.

- LD2 LED 꺼짐
- CAN 버스에 0x100 프레임 없음
- UART 로그: `[BL] Waiting...` 무한 반복

---

### 재현 절차

1. RPi5에서 socketcan 인터페이스로 OTA 실행
2. `RequestDownload` 단계에서 Receive timeout 발생 → OTA 중단
3. DriveECU 전원 재인가
4. Bootloader가 Safe State 진입

---

### 원인 분석

#### 1단계 — OTA 중단 시점 파악

`ota_client.py` 로그:

```
[UDS] DiagnosticSessionControl(Extended)   ← 성공
[UDS] SecurityAccess - seed request        ← 성공
[UDS] Unlocked                             ← 성공
[UDS] RequestDownload  addr=0x08040000     ← 여기서 Receive timeout
```

`RequestDownload` 핸들러는 수신 즉시 Slot B Flash erase를 시작한다. timeout은 응답을 받지 못한 것이지, erase가 수행되지 않은 것이 아니다.

#### 2단계 — 메타데이터 상태 확인

이전 OTA 성공 시 기록된 메타데이터:

| 필드 | 값 |
|---|---|
| active_slot | 1 (Slot B) |
| slot_b_status | PENDING |
| slot_a_status | CONFIRMED |

OTA 중단 후에도 메타데이터는 변경되지 않아 위 상태가 유지됐다.

#### 3단계 — Bootloader 로직 분석

Bootloader의 슬롯 선택 로직:

```
active_slot=1 → slot_b_status=PENDING → Slot B 선택
→ is_valid_app(0x08040000)
→ SP = 0xFFFFFFFF (Flash erase로 인한 0xFF 채움)
→ (0xFF000000 == 0x20000000) → false
→ safe_state() 진입
```

**근본 원인:** `is_valid_app()` 실패 시 slot_a_status=CONFIRMED임에도 Slot A 폴백 없이 바로 Safe State로 진입하는 버그.

```c
/* 수정 전 — 폴백 없음 */
if (!is_valid_app(boot_addr)) {
    safe_state();  // Slot A가 CONFIRMED여도 여기서 끝
    return;
}
```

---

### 수정 내용

#### `Bootloader/Core/Src/bootloader.c`

`is_valid_app()` 실패 시 반대 슬롯이 CONFIRMED이면 폴백하도록 수정:

```c
/* 수정 후 */
if (!is_valid_app(boot_addr)) {
    printf("[BL] No valid app at 0x%08lX, trying fallback\r\n", boot_addr);
    if (boot_addr == SLOT_B_ADDR && meta->slot_a_status == SLOT_CONFIRMED) {
        boot_addr = SLOT_A_ADDR;
        printf("[BL] Fallback to Slot A\r\n");
    } else if (boot_addr == SLOT_A_ADDR && meta->slot_b_status == SLOT_CONFIRMED) {
        boot_addr = SLOT_B_ADDR;
        printf("[BL] Fallback to Slot B\r\n");
    } else {
        safe_state();
        return;
    }
    if (!is_valid_app(boot_addr)) {
        printf("[BL] Fallback slot also invalid, entering Safe State\r\n");
        safe_state();
        return;
    }
}
```

#### `tools/ota_client.py`

RPi5 socketcan 인터페이스 지원 추가:

```bash
# macOS (slcan)
python3 ota_client.py --channel /dev/tty.usbmodemXXXX firmware.bin

# RPi5 (socketcan)
python3 ota_client.py --channel can0 --interface socketcan firmware.bin
```

---

### 검증

Bootloader 수정 후 재플래싱:

```
[BL] Bootloader v1.0
[BL] active_slot=1  A=0xAAAAAAAA  B=0xBBBBBBBB
[BL] No valid app at 0x08040000, trying fallback
[BL] Fallback to Slot A
[BL] Jump to 0x08010000  SP=0x20020000  PC=...
```

→ Slot A 정상 부팅 확인.

이후 RPi5에서 OTA 재시도:

```
[UDS] TransferData: 101 chunks x 256 bytes
[UDS]   100%  block=101
[UDS] Transfer complete — ECU will reboot to Slot B
```

→ Slot B 부팅 및 `[DriveECU v2] Start` 확인.

---

### 교훈

- OTA 세션 중단은 Flash erase만 완료된 채 메타데이터가 PENDING으로 남는 중간 상태를 만든다.
- PENDING 슬롯의 `is_valid_app()` 실패 시 반드시 CONFIRMED 슬롯으로 폴백해야 한다.
- OTA 중단 내성(resilience)은 메타데이터 상태 설계와 Bootloader 폴백 로직이 함께 보장해야 한다.
