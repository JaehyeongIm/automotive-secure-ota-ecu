# Troubleshooting Log

## Phase 7 — SensorECU OTA 랜덤 블록 타임아웃 (ISS-OTA-004)

**날짜:** 2026-05-26  
**상태:** 진행 중 (3차 수정까지 적용, 여전히 재현됨)

---

### 현상

SensorECU OTA 전송 중 특정 블록에서 `ISOTPError: Receive timeout`으로 파이프라인 실패.

- 실패 블록 번호가 매 실행마다 다름 (3, 12, 20, 62, 15 등 관측)
- 동일 바이너리 재시도 시 성공하는 경우 있음 → **확률적 오류**
- DriveECU OTA는 정상 동작

```
[UDS] TransferData: 117 chunks x 256 bytes
[UDS]    12%  block=14
[UDS]    12%  block=15
[OTA] ERROR: Receive timeout     ← block 16 응답 없음
```

---

### 시스템 구성

```
RPi5 (OTA 클라이언트)
  ↕ CAN 버스 (500kbps)
SensorECU (STM32F446RE)  ←── OTA 수신 중
DriveECU (STM32F446RE)   ←── 동시에 CAN 버스에서 heartbeat 전송 중
```

| 항목 | 값 |
|---|---|
| CAN 비트레이트 | 500kbps |
| CAN 프레임 전송 시간 | ~222μs |
| ISO-TP CF 간격 (`ota_client.py time.sleep`) | ~1ms |
| 블록당 CF 수 (258바이트 UDS payload) | 36개 |
| CF 버스트 총 소요 시간 | ~36ms |
| ECU CAN RX FIFO 슬롯 수 | 3슬롯 |
| `ReceiveFifoLocked` 설정 | DISABLE (oldest 덮어씀) |
| DriveECU 0x100 heartbeat 주기 | ~100ms |
| SensorECU TIM3 0x201 주기 | ~100ms |
| SensorECU hcsr04 측정 주기 | 50ms (최대 30ms 블로킹) |

---

### ISO-TP 전송 흐름 (정상 케이스)

```
OTA Client (RPi5)              SensorECU (ECU)
      │                              │
      │──── FF (First Frame) ───────>│  ISR: s_ctx 초기화, FC 전송
      │<─── FC (Flow Control) ───────│
      │                              │
      │──── CF#1 ──────────────────>│  ISR: s_ctx.buf에 누적
      │──── CF#2 ──────────────────>│  ISR
      │  ...  (1ms 간격, 총 36개)   │
      │──── CF#36 ─────────────────>│  ISR: ISO-TP 완성 → g_pending_ready=1
      │                              │  Main loop: uds_process() → handle()
      │                              │    ota_flash_write() ~1ms
      │                              │    printf() ~1ms
      │<─── UDS Response (SF) ───────│  isotp_send() → send_can()
      │                              │
      │  (다음 블록 반복)             │
```

**타임아웃 발생 조건**: ECU가 UDS Response를 전송하지 않거나, 전송해도 OTA 클라이언트가 수신하지 못할 때.

---

### 원인 분석 이력

---

#### ① TX 메일박스 경합 가설 → 배제

**가설**: `send_can()`에서 TX 메일박스 3개가 모두 가득 찰 때 FC 전송 실패

**시도**: `send_can()` 내부에 최대 10ms HAL_OK 재시도 루프 추가

**결과**: 더 자주 실패 (블록 12 타임아웃 → 블록 3으로 이동, 악화)

**배제 근거**:  
`send_can()`은 `isotp_can_rx()` 내부에서 호출되며, 이 함수는 `HAL_CAN_RxFifo0MsgPendingCallback`(CAN RX ISR) 컨텍스트에서 실행된다. ISR 내부에서 10ms를 블로킹하면 그 동안 CF 프레임 10개가 3슬롯 FIFO에 쌓여 오버플로우된다. → **ISR에서 절대 블로킹 금지** → 완전 롤백

---

#### ② ISR 내부 printf 블로킹 → 1차 수정 적용, 부분 개선

**파일**: `SensorECU/Core/Src/main.c`

**버그**: `HAL_CAN_RxFifo0MsgPendingCallback`에서 비-ISOTP 프레임에 대해 `printf()` 호출

```c
// 버그 코드
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data);
    if (rx_header.StdId == ISOTP_RX_CAN_ID) {
        isotp_can_rx(rx_data, (uint8_t)rx_header.DLC);
        return;
    }
    printf("[RX] ID:0x%03lX DLC:%lu ...\r\n", ...);  // ← ISR에서 ~3.5ms 블로킹
}
```

**장애 시나리오**:
- DriveECU 0x100 heartbeat 수신 → ISR에서 `printf` → `HAL_UART_Transmit(HAL_MAX_DELAY)` → ~3.5ms 블로킹
- 그 동안 CF 프레임 3~4개 도착 → FIFO(3슬롯) 오버플로우 → oldest 프레임 소실
- SN 불일치 감지 → `s_ctx.active = 0` → ISO-TP 세션 중단
- ECU가 ISO-TP 메시지를 완성하지 못함 → UDS 응답 미전송 → 5초 타임아웃

**수정 내용**: 비-ISOTP 프레임은 묵시적으로 드롭

```c
// 수정 후
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data);
    if (rx_header.StdId == ISOTP_RX_CAN_ID) {
        isotp_can_rx(rx_data, (uint8_t)rx_header.DLC);
    }
}
```

**결과**: 실패 블록 3~20 → 62로 이동. **부분 개선, 완전 해결 안 됨.**

---

#### ③ CAN 중재 충돌 + AutoRetransmission=DISABLE → 2차 수정 적용, 여전히 실패

**가설**: DriveECU 0x100(높은 우선순위)이 SensorECU 0x7E9 UDS 응답과 동시에 전송되면 중재 패배 → `AutoRetransmission=DISABLE`이므로 재전송 없이 응답 소실

**확률 계산**:
- OTA 총 시간: 117블록 × ~50ms = ~5.85초
- DriveECU heartbeat 횟수: ~59회
- 충돌 확률: ~222μs / 50ms × 59 ≈ 23% (관측 50~70%와 같은 차수)

**수정 내용**: `MX_CAN1_Init()`에서 `AutoRetransmission = ENABLE`

```c
hcan1.Init.AutoRetransmission = ENABLE;
```

**결과**: 여전히 실패 (블록 15 타임아웃). **해결 안 됨.**

---

### 현재 상태 (3차 수정까지 적용 후에도 재현)

적용된 수정:
1. ✅ ISR 내 printf 제거
2. ✅ isotp.c 재시도 루프 롤백
3. ✅ AutoRetransmission = ENABLE

그럼에도 여전히 랜덤 블록에서 타임아웃 발생. 실패 블록 번호: 3, 12, 20, 62, **15** (최근).

---

### 미확인 잔여 원인 후보

현재 코드 분석으로는 정확한 원인을 특정하지 못하는 상태. 다음 후보들이 남아 있음:

#### 후보 A: `hcsr04_measure_cm()` 30ms 블로킹 (SensorECU vs DriveECU 구조 차이)

- DriveECU main loop: `uds_process()`만 실행 (블로킹 없음)
- SensorECU main loop: 매 50ms마다 `hcsr04_measure_cm()` 호출 (최대 30ms 블로킹)
- hcsr04 블로킹 중 ISR는 정상 실행되지만, main loop이 멈춰 있는 동안 추가 간섭 가능성

#### 후보 B: CF 전송 간격 1ms 마진 부족

- `ota_client.py`의 `time.sleep(0.001)`: Linux 비실시간 스케줄러에서 실제 간격이 불규칙
- Jenkins(Java) + Python 동시 실행 시 RPi5 CPU 경합 → time.sleep 정확도 저하
- 1ms 간격은 CAN RX FIFO 3슬롯 기준으로 마진이 거의 없음

#### 후보 C: 진단 정보 부족

- ECU UART 출력에서 실패 시점에 어떤 로그가 출력되는지 확인 불가
- SN 불일치 발생 여부 / TX 응답 전송 여부 / flash_write HAL_ERROR 여부 모름

---

### 다음 진단/수정 방향 (미적용)

| 우선순위 | 방향 | 내용 |
|---|---|---|
| 1 | OTA 중 hcsr04 정지 | `uds_ota_active()` 플래그 추가, `g_state == STATE_DOWNLOADING`이면 hcsr04 스킵 |
| 2 | CF 전송 간격 증가 | `ota_client.py` `time.sleep(0.001)` → `time.sleep(0.005)` |
| 3 | ECU 진단 로그 추가 | `isotp_can_rx` SN 불일치 발생 시 volatile 플래그 세팅 → main loop에서 출력 |

---

### 교훈 (확정된 것)

#### ISR에서 절대 블로킹 함수 호출 금지

| 금지 패턴 | 이유 |
|---|---|
| `printf()` in ISR | `HAL_UART_Transmit(HAL_MAX_DELAY)` → 수ms 블로킹 → FIFO 오버플로우 |
| `HAL_Delay()` in ISR | SysTick 동급/하위 우선순위 시 영원히 대기 |
| 재시도 루프 in ISR | 루프 기간 동안 동급/하위 인터럽트 차단 |

#### STM32F4 CAN FIFO 동작

- `ReceiveFifoLocked = DISABLE`: FIFO 가득 찬 상태에서 새 프레임 도착 시 oldest 프레임이 새 프레임에 덮어씌워짐
- 결과: oldest 프레임 소실 → ISO-TP SN 순서 깨짐 → `s_ctx.active = 0` → 세션 중단

#### CAN AutoRetransmission

- OTA처럼 신뢰성이 중요한 전송: `ENABLE` 권장
- 중재 패배(arbitration loss)만으로는 ECU가 error passive/bus-off로 가지 않음
- `ENABLE` 시 중재 패배 후 버스 IDLE 되는 순간 (~222μs 후) 자동 재전송

---

### 관련 파일

- `SensorECU/Core/Src/main.c` — ISR `printf` 제거, `AutoRetransmission = ENABLE`
- `SensorECU/Core/Src/isotp.c` — ISR 내 재시도 루프 롤백
- `tools/ota_client.py` — CF 간격 `time.sleep(0.001)` 사용 중

---

### 관련 문서

- [phase6_rc_car_assembly_can_failure.md](phase6_rc_car_assembly_can_failure.md) — 직전 트러블슈팅
- [phase5_slot_b_can_failure.md](phase5_slot_b_can_failure.md) — ISR 내 `__enable_irq()` 누락 사례
