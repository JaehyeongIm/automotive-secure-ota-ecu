# Troubleshooting Log

## Phase 5B — Slot B 부팅 후 CAN 통신 완전 불통 (ISS-CAN-003)

**날짜:** 2026-05-21

---

### D2. 문제 정의 — 현상

RPi5에서 A/B OTA + ECDSA 서명 검증 성공 후 Slot B 펌웨어가 부팅됐으나 CAN 통신이 완전히 동작하지 않는다.

- UART: `[BL] ECDSA OK` → `[BL] Jump to 0x08040000` → `[DriveECU v2] Start` 정상 출력
- CAN 버스에 0x100(heartbeat) 프레임 없음
- SensorECU 0x201 프레임 송신 중이지만 DriveECU에서 `[RX]` 로그 없음
- main 루프는 실행 중 (IWDG 리셋 반복 없음, UART 완전 중단 없음)

---

### D2. 재현 절차

1. STM32CubeProgrammer로 Sector 2(0x08008000, Metadata) erase → 메타데이터 초기화
2. Slot A(기본) 부팅, CAN 정상 동작 확인
3. RPi5에서 `ota_client.py`로 OTA 실행 → Slot B에 `DriveECU_SlotB.bin`(서명 포함) 전송
4. ECU 재부팅 → Bootloader: `active_slot=1, slot_b_status=PENDING` → Slot B 선택
5. Bootloader ECDSA 검증 통과, Slot B 점프
6. `[DriveECU v2] Start` 이후 CAN 무응답

---

### 플래시 상태 (재현 시점)

| 필드 | 값 |
|---|---|
| active_slot | 1 (Slot B) |
| slot_b_status | PENDING |
| slot_a_status | CONFIRMED |
| boot_addr | 0x08040000 |

---

### D4. 근본 원인 분석 — 배제된 원인 (Is–Is Not)

| 가설 | 배제 근거 |
|---|---|
| VTOR 미설정으로 인한 하드폴트 | `main.c` USER CODE BEGIN 1에 `SCB->VTOR = (uint32_t)&g_pfnVectors` 적용. 하드폴트 시 UART 자체가 죽거나 IWDG 리셋 반복 발생하나 해당 현상 없음 |
| CAN 하드웨어 불량 | Slot A에서는 CAN 정상, SensorECU 0x201도 버스에서 관측됨 |
| IWDG 타임아웃 루프 | `[DriveECU v2] Start` 단발 출력 후 반복 없음 — 루프 없이 main에 진입한 것으로 확인 |
| Bootloader HAL_DeInit() 누락 | `jump_to_app()`에서 `HAL_DeInit()` 호출 확인, DriveECU MX_CAN1_Init()이 CAN 레지스터 재초기화해야 함 |
| 링커 주소 오설정 | Slot B용 `STM32F446RETX_FLASH_SlotB.ld` 사용, FLASH ORIGIN=0x08040000 확인 |

---

### D4. 근본 원인

**`__enable_irq()` 누락 — 인터럽트 전역 비활성 상태로 Slot B 진입**

부트로더 `jump_to_app()`이 `__disable_irq()` 호출 후 PRIMASK=1 상태 그대로 점프하는데,
Slot B 바이너리(May 18 빌드)는 `USER CODE BEGIN 1`에 `__enable_irq()` 복원 코드가
포함되기 전(commit `926ea68`, May 19) 빌드된 것이었다.

- CAN RX 인터럽트(`HAL_CAN_RxFifo0MsgPendingCallback`) → PRIMASK=1로 차단
- TIM2 heartbeat TX 콜백 → 동일하게 차단
- UART `printf` → 폴링 방식이라 영향 없음 (그래서 `[DriveECU v2] Start`는 출력됨)

**소스 코드에 fix가 있었으나 Slot B 플래시 바이너리가 갱신되지 않은 상태였음:**

| 날짜 | 내용 |
|---|---|
| May 18 | Slot B 바이너리 빌드 (`__enable_irq()` 없음) |
| May 19 | `926ea68`: `__enable_irq()` 소스에 추가 |
| May 20 | 구버전 바이너리(May 18) 서명 후 OTA → Slot B 여전히 구버전 |
| May 21 | 새 빌드(`SlotB/DriveECU.bin`) OTA → 해결 |

---

### D5–D6. 시정 조치 & 검증

`SlotB/DriveECU.bin` (May 21 빌드, `__enable_irq()` 포함) OTA 후 정상 동작 확인:

```
[CAN] ConfigFilter=0
[CAN] Start=0 state=2 ESR=0x00000000
[CAN] Notify=0
[DriveECU v2] Start
[RX] ID:0x201 DLC:8 Data:01 00 02 9A cnt:170608
```

---

### 원인 후보 (진단 전)

1. **`HAL_CAN_Start()` HAL_TIMEOUT 또는 HAL_ERROR 반환**
2. **CAN Bus-Off (ESR BOFF 비트 세트)**
3. **`HAL_CAN_AddTxMessage()` 묵시적 실패**

→ 진단 코드 실행 결과 모두 정상(`Start=0, ESR=0x00000000`). 실제 원인은 위의 `__enable_irq()` 누락.

---

### 진단 코드 (추가 완료)

`DriveECU/Core/Src/main.c`에 다음 진단 출력 추가:

#### CAN 초기화 결과 확인 (USER CODE BEGIN 2)

```c
{
    HAL_StatusTypeDef r;
    r = HAL_CAN_ConfigFilter(&hcan1, &filter);
    printf("[CAN] ConfigFilter=%d\r\n", (int)r);
    r = HAL_CAN_Start(&hcan1);
    printf("[CAN] Start=%d state=%lu ESR=0x%08lX\r\n",
           (int)r, (uint32_t)hcan1.State, CAN1->ESR);
    r = HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);
    printf("[CAN] Notify=%d\r\n", (int)r);
}
```

#### 주기적 상태 확인 (USER CODE BEGIN 3, main while loop)

```c
static uint32_t s_last_tick = 0;
uint32_t now = HAL_GetTick();
if (now - s_last_tick >= 3000) {
    s_last_tick = now;
    printf("[ALIVE] t=%lu CAN state=%lu ESR=0x%08lX\r\n",
           now, (uint32_t)hcan1.State, CAN1->ESR);
}
```

#### TIM2 TX 실패 감지 (USER CODE BEGIN 4)

```c
} else {
    static uint8_t s_once = 0;
    if (!s_once) {
        s_once = 1;
        printf("[TIM2] CAN TX fail: state=%lu ESR=0x%08lX err=0x%08lX\r\n",
               (uint32_t)hcan1.State, CAN1->ESR, hcan1.ErrorCode);
    }
}
```

---

### 진단 해석 기준

| 출력 패턴 | 의미 |
|---|---|
| `[CAN] Start=1` (HAL_ERROR) | `HAL_CAN_Start()` 실패. BOFF 또는 하드웨어 초기화 문제 |
| `ESR=0x00000004` (BOFF 비트) | CAN Bus-Off 상태. 버스에서 ECU가 분리됨 |
| `[CAN] Start=0` + `[TIM2] CAN TX fail` | 초기화는 성공했으나 TX 큐 실패 (mailbox full 등) |
| `[CAN] Start=0` + `[ALIVE]` 정상 + TX fail 없음 | 초기화 정상, 문제는 상위 로직에 있음 |

---

### 다음 진단 절차

1. **Slot A 상태로 초기화**
   - STM32CubeProgrammer → Sector 2(0x08008000, 16KB) Erase
   - ECU 재부팅, Slot A 정상 부팅 확인

2. **진단용 Slot B 바이너리 빌드 및 OTA**
   - STM32CubeIDE: DriveECU → `STM32F446RETX_FLASH_SlotB.ld` 링커 설정으로 빌드
   - `python3 tools/sign_firmware.py DriveECU.bin firmware_signed.bin`
   - `python3 tools/ota_client.py --channel can0 --interface socketcan firmware_signed.bin`

3. **UART 출력으로 근본 원인 판별**
   - `[CAN] Start=?` 및 `ESR=0x?` 값 기록
   - 진단 해석 기준 표 참조

4. **원인별 후속 조치**
   - HAL_TIMEOUT: `MX_CAN1_Init()` 프리스케일러/타이밍 파라미터 Slot B 링커 주소에서 재검증
   - Bus-Off: `HAL_CAN_ResetError()` 호출 후 `HAL_CAN_Start()` 재시도 로직 추가
   - TX 실패: `HAL_CAN_GetTxMailboxesFreeLevel()` 확인, mailbox 대기 로직 점검

---

### 관련 문서

- [구현절차.md](../assets/구현계획.md) — Phase 5B 상태
- [ISS-BL-002_fallback-pending-invalid.md](ISS-BL-002_fallback-pending-invalid.md) — 직전 트러블슈팅 (Bootloader 폴백 버그)
