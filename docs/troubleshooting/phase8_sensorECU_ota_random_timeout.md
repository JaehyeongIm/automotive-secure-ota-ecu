# Troubleshooting Log

## Phase 8 — SensorECU OTA 랜덤 블록 타임아웃 (ISS-OTA-004)

**날짜:** 2026-05-26  
**상태:** 원인 재분석 중 — hcsr04 가설 약화, CF 간격이 실제 원인일 가능성 높음 (2026-05-27)

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

### 현재 상태 (4차 수정 적용 후 재검증)

적용된 수정:
1. ✅ ISR 내 printf 제거
2. ✅ isotp.c 재시도 루프 롤백
3. ✅ AutoRetransmission = ENABLE
4. ✅ OTA 중 hcsr04 차단 + CF 간격 1ms → 5ms

실패 블록 번호 이력: 3, 12, 20, 62, 15, **62** (최근). 4차 수정 후 파이프라인 SUCCESS 확인.

---

### 원인 재분석 (2026-05-27) — CF 간격이 실제 원인일 가능성

4차 수정은 두 가지를 동시에 적용했다: **(a) hcsr04 차단** + **(b) CF 간격 1ms → 5ms**. 이후 추가 검증에서 다음이 확인됐다.

| 테스트 조건 | 결과 |
|---|---|
| hcsr04 차단 없음 + CF 5ms | **성공** (117블록 타임아웃 없음) |
| hcsr04 차단 없음 + CF 0.003s | 실패 |
| hcsr04 차단 없음 + CF 0.001s | 실패 |

**hcsr04 차단 없이 CF 5ms만으로 OTA가 성공했다.** 이는 hcsr04 블로킹이 근본 원인이 아닐 수 있음을 시사한다.

#### CF 간격이 결정적인 이유

500kbps CAN에서 프레임 1개 전송 시간 ≈ 222μs. CF 간격이 5ms면 다음 프레임 도착 전까지 메일박스 3개가 모두 비워질 시간이 충분하다.

```
CF 간격 1ms: 1ms 안에 배경 트래픽(0x201, 0x200) + OTA 응답(0x7E9) 경합
             → 3개 메일박스 동시 포화 가능 → 응답 소실
             
CF 간격 5ms: 각 프레임 사이 5ms 여유
             → 배경 트래픽(100ms + 50ms 주기)은 5ms 창에 평균 0.1개
             → 메일박스 포화 거의 불가능 → 응답 안정 전달
```

hcsr04가 30ms 블로킹을 해도, 그 30ms 동안 배경 CAN 트래픽은 최대 1~2개에 불과해 3개 메일박스를 포화시키지 못한다. **CF 1ms 간격이 메일박스 포화의 직접 원인이었을 가능성이 높다.**

> 이 가설을 명확히 특정하기 위해 `--cf-delay` 파라미터와 `TX_FAIL_DURING_OTA` 진단 코드를 추가하여 검증 중 (phase8 관련 작업과 병행).

---

### 근본 원인 (가설 수정)

#### TX 메일박스 동시 점유 → `HAL_CAN_AddTxMessage` HAL_ERROR → 응답 소실

`[UDS] Block 62` 로그 출력 후 타임아웃 발생 → `handle()` 내 `printf`까지는 정상 실행됐음을 의미. 즉, `isotp_send()` → `send_can()` → `HAL_CAN_AddTxMessage()` 시점에 TX 메일박스 3개가 모두 점유된 상태.

**구체적 시나리오:**

```
T+0ms  : 50ms 틱 → hcsr04_measure_cm() 진입 (최대 30ms 블로킹)
T+Xms  : TIM3 ISR 발화 → HAL_CAN_AddTxMessage(0x201) → mailbox[0] 점유
T+30ms : hcsr04 완료 → HAL_CAN_AddTxMessage(0x200 obs_data) → mailbox[1] 점유
         DriveECU 0x100 중재로 0x200/0x201 전송 지연 → mailbox 점유 유지
T+30ms+ε: 블록 N 마지막 CF 수신 → g_pending_ready=1
T+33ms : uds_process() → handle() → isotp_send() 호출 시:
         직전 FC(0x7E9)가 DriveECU 0x100에 중재 패배하여 mailbox[2]에서 재전송 대기 중
         → HAL_CAN_AddTxMessage(응답 0x7E9) → HAL_ERROR (3개 모두 점유)
         → send_can()은 리턴값 무시 → 응답 프레임 소실
T+5s   : RPi5 타임아웃 → ISOTPError("Receive timeout")
```

**확률적 발생 이유**: hcsr04 50ms 주기와 블록 처리 ~40ms 타이밍이 무작위로 겹칠 때만 발생.

**DriveECU 중재 단독으로는 OTA 실패하지 않음**: AutoRetransmission=ENABLE 상태에서 중재 패배 후 재전송까지 ~444μs 소요. RPi5 5초 타임아웃 대비 무시 가능한 시간. 단, 재전송 대기 중 메일박스를 계속 점유하므로 다른 프레임이 동시에 쌓이면 3개가 모두 차는 조건이 됨.

---

### 4차 수정 내용

#### ④ OTA 중 hcsr04 차단 + CF 간격 증가 → 4차 수정 적용, 검증 대기

**수정 내용**:

`SensorECU/Core/Src/uds.c` — `uds_ota_active()` 추가:

```c
int uds_ota_active(void)
{
    return g_state == STATE_DOWNLOADING;
}
```

`SensorECU/Core/Inc/uds.h` — 선언 추가:

```c
int uds_ota_active(void);
```

`SensorECU/Core/Src/main.c` — hcsr04 블록 조건 추가:

```c
// 수정 전
if (HAL_GetTick() - s_measure_tick >= 50) {

// 수정 후
if (!uds_ota_active() && HAL_GetTick() - s_measure_tick >= 50) {
```

`tools/ota_client.py` — CF 간격 증가:

```python
# 수정 전
time.sleep(0.001)

# 수정 후
time.sleep(0.005)
```

**효과**: OTA 중 메인 루프는 `IWDG_Refresh → uds_process()` 반복만 실행. TX 메일박스는 TIM3 0x201 heartbeat(100ms 주기) 1개만 간헐적으로 사용 → `isotp_send()` 호출 시 항상 여유 메일박스 존재. OTA 완료 시 `NVIC_SystemReset()`으로 재부팅하므로 hcsr04 일시 정지는 문제 없음.

**검증 결과**: 4차 수정 적용 후 파이프라인 실행 → 117블록 타임아웃 없이 전송 완료, SlotB 부팅 확인. `Finished: SUCCESS`

---

### 교훈

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
- `SensorECU/Core/Src/isotp.c` — ISR 내 재시도 루프 롤백, TX fail 카운터 추가
- `SensorECU/Core/Src/uds.c` — `TX_FAIL_DURING_OTA` 진단 출력 추가
- `tools/ota_client.py` — `--cf-delay` 인자 추가 (기본 0.005s)

---

### 관련 문서

- [phase9_rc_car_assembly_can_failure.md](phase9_rc_car_assembly_can_failure.md) — 직전 트러블슈팅
- [phase5_slot_b_can_failure.md](phase5_slot_b_can_failure.md) — ISR 내 `__enable_irq()` 누락 사례
- [phase8_no_heartbeat_after_failed_ota.md](phase8_no_heartbeat_after_failed_ota.md) — OTA 실패 후 하트비트 없음 (파생 이슈)
