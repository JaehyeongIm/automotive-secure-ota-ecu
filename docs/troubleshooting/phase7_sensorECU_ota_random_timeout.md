# Troubleshooting Log

## Phase 7 — SensorECU OTA 랜덤 블록 타임아웃 (ISS-OTA-004)

**날짜:** 2026-05-26

---

### 현상

SensorECU OTA 전송 중 특정 블록(3, 12, 20번 등 무작위)에서 `ISOTPError: Receive timeout`으로 파이프라인 실패.

- 재현 조건: OTA 시도 시 50~70% 확률로 발생, 실패 블록은 매번 다름
- `[UDS] Block N` 로그가 해당 블록에서 출력되지 않음 (ECU가 메시지 자체를 수신 못 함)
- 동일 바이너리 재시도 시 성공하는 경우 있음

```
[UDS] TransferData: 56 chunks x 256 bytes
[UDS]   100%  block=3
[OTA] ERROR: Receive timeout      ← block 4 이후 응답 없음
```

---

### 재현 절차

1. Jenkins 파이프라인에서 SensorECU OTA 실행 (DriveECU OTA 완료 후)
2. `ota_client.py`가 256바이트 × N 블록을 UDS `0x36 TransferData`로 전송
3. 일부 블록에서 ECU 응답 없음 → `_recv_msg()` 5초 타임아웃

---

### 원인 분석 과정

#### 1단계: TX 메일박스 경합 가설 (배제)

**가설**: CAN TX 메일박스 3개가 모두 가득 찰 때 FC(Flow Control) 전송 실패 → 묵시적 skip

**시도**: `send_can()`에 HAL_CAN_AddTxMessage 실패 시 최대 10ms 재시도 루프 추가

**결과**: 오히려 더 자주 실패. 블록 12에서 타임아웃.

**배제 이유**:
- `send_can()`은 CAN RX ISR 내부(`HAL_CAN_RxFifo0MsgPendingCallback` → `isotp_can_rx()` → First Frame 처리)에서 FC 전송 시 호출됨
- ISR 안에서 10ms 블로킹 = RX FIFO 오버플로우 유발 (1ms 간격 CF × 10개 ≥ 3슬롯 FIFO)
- 해당 변경 완전 롤백

#### 2단계: TIM3 ISR CAN 경합 가설 (배제)

**가설**: TIM3가 100ms마다 `HAL_CAN_AddTxMessage` 호출 → TX 메일박스 경합

**확인**: `stm32f4xx_it.c` 검토 결과 TIM3 ISR은 `HAL_TIM_IRQHandler` 호출뿐.
`HAL_TIM_PeriodElapsedCallback`에서 `HAL_CAN_AddTxMessage` 호출은 하지만:
- TIM3 주기 = (8999+1) × (999+1) / 90MHz = 100ms → 매우 낮은 빈도
- 실패 시 단순히 LED 토글만 skip → 차단 없음

#### 3단계: 근본 원인 확인 — ISR 내부 `printf` 블로킹

**파일**: `SensorECU/Core/Src/main.c`, `HAL_CAN_RxFifo0MsgPendingCallback`

```c
// 수정 전 (버그 코드)
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data);
    if (rx_header.StdId == ISOTP_RX_CAN_ID) {
        isotp_can_rx(rx_data, (uint8_t)rx_header.DLC);
        return;
    }
    printf("[RX] ID:0x%03lX ...\r\n", ...);  // ← ISR 내부에서 블로킹!
}
```

**타이밍 분석**:

| 항목 | 값 |
|---|---|
| CAN 비트레이트 | 500kbps |
| 프레임 간격 (ota_client.py `time.sleep(0.001)`) | 1ms |
| 하나의 CAN 프레임 전송 시간 | ~222μs |
| UART 115200baud, printf 40자 | ~3.5ms 블로킹 |
| CAN RX FIFO 슬롯 수 | 3슬롯 |
| `ReceiveFifoLocked` | DISABLE (가득 차면 oldest 덮어씀) |

**장애 시나리오**:

1. DriveECU가 0x100 heartbeat 프레임 전송 (100ms 주기)
2. SensorECU RX FIFO에 0x100 프레임 수신 → ISR 진입
3. `printf("[RX] ...")` 실행 → `HAL_UART_Transmit(HAL_MAX_DELAY)` → **약 3.5ms 블로킹**
4. 이 3.5ms 동안 ota_client.py가 CF 프레임 3~4개 전송 (1ms 간격)
5. 하드웨어 FIFO 3슬롯 가득 참 → 4번째 CF 프레임 덮어씀 (oldest 소실)
6. ISR 복귀 후 이전 프레임 처리 → ISO-TP SN 불일치 감지 → `s_ctx.active = 0`
7. 이후 모든 CF 무시 → ISO-TP 메시지 완성 안됨 → `uds_on_isotp_rx` 미호출
8. UDS 응답 없음 → OTA 클라이언트 5초 타임아웃

**왜 "랜덤" 블록인가**:
- DriveECU 0x100 heartbeat와 OTA CF 버스트(~36ms)가 시간적으로 겹치는 경우에만 발생
- 블록당 약 36ms 지속, DriveECU 주기 100ms → 겹침 확률 약 36% (실제 50~70% 실패율과 부합)

---

### 해결

`SensorECU/Core/Src/main.c`의 `HAL_CAN_RxFifo0MsgPendingCallback`에서 비-ISO-TP 프레임에 대한 `printf` 제거:

```c
// 수정 후
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data);
    if (rx_header.StdId == ISOTP_RX_CAN_ID) {
        isotp_can_rx(rx_data, (uint8_t)rx_header.DLC);
    }
    /* Non-ISOTP frames are silently dropped.
     * printf() here would block ~4ms (HAL_UART_Transmit with HAL_MAX_DELAY),
     * causing RX FIFO overflow and ISO-TP SN mismatches during OTA. */
}
```

---

### 교훈

**ISR 내부에서 절대 블로킹 함수 호출 금지**

| 금지 패턴 | 이유 |
|---|---|
| `printf()` in ISR | `HAL_UART_Transmit(HAL_MAX_DELAY)` → 수ms 블로킹 |
| `HAL_Delay()` in ISR | SysTick이 같은 또는 낮은 우선순위면 영원히 대기 |
| 재시도 루프 in ISR | 루프 기간 동안 동급/하위 인터럽트 차단 → FIFO 오버플로우 |

**STM32F4 CAN FIFO 동작 (ReceiveFifoLocked=DISABLE)**:
- FIFO가 가득 찬 상태에서 새 프레임 수신 시 가장 오래된 프레임(lowest index)을 새 프레임이 덮어씀
- 즉, oldest 프레임 소실 → ISO-TP SN 순서 깨짐

---

### 관련 파일

- `SensorECU/Core/Src/main.c` — `HAL_CAN_RxFifo0MsgPendingCallback` 수정
- `SensorECU/Core/Src/isotp.c` — ISO-TP SN 검증 로직 (`s_ctx.active = 0` on mismatch)
- `tools/ota_client.py` — CF 간격 `time.sleep(0.001)` = 1ms

---

### 관련 문서

- [phase6_rc_car_assembly_can_failure.md](phase6_rc_car_assembly_can_failure.md) — 직전 트러블슈팅
- [phase5_slot_b_can_failure.md](phase5_slot_b_can_failure.md) — ISR 내부 `__enable_irq()` 누락 관련 사례
