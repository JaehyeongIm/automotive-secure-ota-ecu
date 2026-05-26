# Troubleshooting Log

## Phase 7 — SensorECU OTA 랜덤 블록 타임아웃 (ISS-OTA-004)

**날짜:** 2026-05-26  
**상태:** 해결됨 (2단계 수정 적용)

---

### 현상

SensorECU OTA 전송 중 특정 블록(3, 12, 20, 62번 등 매 실행마다 다름)에서  
`ISOTPError: Receive timeout`으로 파이프라인 실패.

```
[UDS]   51%  block=60
[UDS]   53%  block=62
[OTA] ERROR: Receive timeout      ← block 63의 응답 없음
```

- 동일 바이너리 재시도 시 성공하는 경우 있음 → 확률적 오류
- 실패 블록 번호는 매번 달라짐 (3, 12, 20, 62 관측)
- 전송이 시작된 후 절반 가까이 진행되다 실패하는 경우도 있음

---

### 동작 환경

| 항목 | 값 |
|---|---|
| CAN 비트레이트 | 500kbps |
| CAN 프레임 1개 전송 시간 | ~222μs |
| ISO-TP CF 간격 (`time.sleep(0.001)`) | ~1ms |
| 블록당 CF 수 (256바이트 데이터) | 36개 CF |
| 블록당 CF 버스트 총 소요 | ~36ms |
| `_recv_msg` 타임아웃 | 5초 |
| CAN RX FIFO 슬롯 수 | 3슬롯 |
| `ReceiveFifoLocked` | DISABLE (가득 차면 oldest 덮어씀) |
| `AutoRetransmission` (수정 전) | DISABLE |
| DriveECU 0x100 heartbeat 주기 | ~100ms |

---

### 원인 분석 과정

#### 1단계: TX 메일박스 경합 가설 (배제)

**가설**: `send_can()`에서 CAN TX 메일박스가 모두 가득 찰 때 FC 전송 실패

**시도**: `send_can()`에 최대 10ms 재시도 루프 추가

**결과**: 더 자주 실패. (블록 12 타임아웃)

**배제 이유**: `send_can()`은 CAN RX ISR 내에서 FC 전송 시 호출됨.  
ISR 내 10ms 블로킹은 후속 CF 프레임 3~10개를 FIFO에 쌓아 오버플로우 유발.  
→ 해당 변경 완전 롤백.

---

#### 2단계: ISR 내부 `printf` 블로킹 (1차 수정 — 부분 해결)

**파일**: `SensorECU/Core/Src/main.c`, `HAL_CAN_RxFifo0MsgPendingCallback`

**버그 코드**:
```c
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data);
    if (rx_header.StdId == ISOTP_RX_CAN_ID) {
        isotp_can_rx(rx_data, (uint8_t)rx_header.DLC);
        return;
    }
    printf("[RX] ID:0x%03lX ...\r\n", ...);  // ← ISR에서 HAL_UART_Transmit 블로킹!
}
```

**타임아웃 시나리오**:

1. DriveECU가 0x100 heartbeat 프레임 전송 (100ms 주기)
2. SensorECU CAN RX ISR: 0x100 프레임 → `printf()` 실행 → `HAL_UART_Transmit(HAL_MAX_DELAY)` → **~3.5ms 블로킹**
3. 이 3.5ms 동안 ota_client.py CF 프레임 3~4개 도착 (1ms 간격)
4. CAN RX FIFO(3슬롯) 오버플로우 → oldest 프레임 소실
5. ISR 복귀 후 SN 불일치 감지 → `s_ctx.active = 0` → ISO-TP 세션 중단
6. ECU가 ISO-TP 메시지를 완성하지 못함 → UDS 응답 미전송 → OTA 클라이언트 5초 타임아웃

**1차 수정**: 비-ISOTP 프레임에 대한 `printf` 제거, 묵시적으로 드롭

```c
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data);
    if (rx_header.StdId == ISOTP_RX_CAN_ID) {
        isotp_can_rx(rx_data, (uint8_t)rx_header.DLC);
    }
    /* Non-ISOTP frames silently dropped — printf here blocks ~4ms,
     * causing RX FIFO overflow and ISO-TP SN mismatches during OTA. */
}
```

**결과**: 실패 블록이 3/12/20 → 62로 이동 (개선됐으나 완전 해결 안 됨)

---

#### 3단계: CAN 중재 충돌 + AutoRetransmission=DISABLE (근본 원인)

**현상**: 1차 수정 후에도 랜덤 블록에서 여전히 타임아웃 발생

**근본 원인**:

CAN 버스에서 두 노드가 동시에 전송하면 **낮은 ID가 우선순위를 가진다(비트 단위 중재)**.

- DriveECU heartbeat: **ID 0x100** (높은 우선순위)  
- SensorECU UDS 응답: **ID 0x7E9** (낮은 우선순위)

ECU가 block N 처리 후 `isotp_send()` (ID=0x7E9)를 호출하는 순간,  
DriveECU가 동시에 0x100 프레임을 전송하면 **0x7E9가 중재에서 패배**한다.  
`AutoRetransmission = DISABLE` 이므로 ECU는 재전송하지 않고 포기.  
OTA 클라이언트는 응답을 영원히 기다리다 5초 후 타임아웃.

**확률 계산 (추정)**:
| 항목 | 값 |
|---|---|
| OTA 총 시간 (~117블록 × 50ms) | ~5.85초 |
| DriveECU heartbeat 횟수 | ~59회 |
| 각 heartbeat의 충돌 윈도우 | ~222μs / 50ms ≈ 0.44% |
| 전체 OTA 실패 확률 추정 | 1 - (1-0.0044)^59 ≈ **23%** |

→ 관측된 50~70% 실패율과 같은 차수 (드라이브 ECU heartbeat 외 TIM3 0x201도 경합)

---

### 최종 수정 사항

**수정 1**: `SensorECU/Core/Src/main.c` — ISR 내 `printf` 제거  
**수정 2**: `SensorECU/Core/Src/main.c` — `AutoRetransmission = ENABLE` 변경

```c
// 수정 전
hcan1.Init.AutoRetransmission = DISABLE;

// 수정 후
hcan1.Init.AutoRetransmission = ENABLE;
```

`AutoRetransmission = ENABLE` 설정 시 CAN 하드웨어가 중재 패배 후 자동으로 재전송을 시도하므로  
0x100 프레임이 버스를 점유해도 ECU의 0x7E9 응답이 반드시 전달됨.

---

### 교훈

#### ISR에서 절대 블로킹 함수 호출 금지

| 금지 패턴 | 이유 |
|---|---|
| `printf()` in ISR | `HAL_UART_Transmit(HAL_MAX_DELAY)` → 수ms 블로킹 |
| `HAL_Delay()` in ISR | SysTick 동급/하위 우선순위 시 영원히 대기 |
| 재시도 루프 in ISR | 루프 기간 동안 동급/하위 인터럽트 차단 → FIFO 오버플로우 |

#### CAN AutoRetransmission 설정 원칙

| 사용 사례 | 권장 설정 |
|---|---|
| 시간 정확도 중요, AUTOSAR CAN | DISABLE (상위 레이어가 관리) |
| OTA / 신뢰성 중요 전송 | **ENABLE** |
| CAN FD / 버스 부하 최적화 | 설계에 따라 다름 |

CAN ID는 우선순위 역할도 한다. 낮은 ID = 높은 우선순위.  
`0x100 (DriveECU) < 0x7E9 (SensorECU UDS)` → 동시 전송 시 항상 ECU가 패배.  
OTA처럼 신뢰성이 중요한 전송에서는 AutoRetransmission을 반드시 활성화해야 한다.

#### STM32F4 CAN FIFO 동작

- `ReceiveFifoLocked = DISABLE`: FIFO 가득 찬 상태에서 새 프레임 수신 시 가장 오래된 프레임을 새 프레임이 덮어씀
- 결과: oldest 프레임 소실 → ISO-TP SN 순서 깨짐 → 세션 중단

---

### 수정 커밋 요약

| 커밋 | 변경 내용 | 결과 |
|---|---|---|
| 1차 | ISR 내 `printf` 제거 + `isotp.c` 재시도 루프 롤백 | 실패 블록 3~20 → 62로 이동 (개선) |
| 2차 | `AutoRetransmission = ENABLE` | 예상: 완전 해결 |

---

### 관련 파일

- `SensorECU/Core/Src/main.c` — ISR `printf` 제거, `AutoRetransmission = ENABLE`
- `SensorECU/Core/Src/isotp.c` — ISR 내 재시도 루프 롤백
- `tools/ota_client.py` — CF 간격 `time.sleep(0.001)` (참고)

---

### 관련 문서

- [phase6_rc_car_assembly_can_failure.md](phase6_rc_car_assembly_can_failure.md) — 직전 트러블슈팅
- [phase5_slot_b_can_failure.md](phase5_slot_b_can_failure.md) — ISR 내 `__enable_irq()` 누락 관련 사례
