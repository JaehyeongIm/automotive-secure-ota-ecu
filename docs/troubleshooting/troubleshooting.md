# Troubleshooting Log

## Phase 4 — UDS/ISO-TP OTA: DriveECU CAN 통신 단절

**날짜:** 2026-05-18 ~ 2026-05-19
**상태:** ✅ 해결됨
**증상:** `ota_client.py` 실행 시 `[OTA] ERROR: Receive timeout` 발생

---

### 현상 요약

```
python tools/ota_client.py --channel /dev/tty.usbmodem2069368D39451 DriveECU/Debug/DriveECU_slotB.bin

[OTA] Firmware: DriveECU/Debug/DriveECU_slotB.bin  (25672 bytes)
[UDS] DiagnosticSessionControl(Extended)
[OTA] ERROR: Receive timeout
```

추가 관찰:
- LD2 LED가 켜져 있지만 깜빡이지 않음 (CAN TX 미발생)
- `[DriveECU v2] Start` UART 로그는 정상 출력 → 앱은 실행 중

---

### 진단 과정

#### 1단계 — 포트 혼동 (해결됨)

처음에 CANable 포트를 잘못 지정했다.

| 포트 | 실제 장치 |
|---|---|
| `/dev/tty.usbmodem21103` | DriveECU ST-Link UART |
| `/dev/tty.usbmodem21203` | SensorECU ST-Link UART |
| `/dev/tty.usbmodem2069368D39451` | **CANable (slcan)** ← 올바른 포트 |

`ls /dev/tty.usb*` 명령으로 포트를 구분해야 한다.

#### 2단계 — CAN 버스 모니터링

`can_monitor.py`로 버스 전체를 모니터링한 결과:

```
18:16:53.535  0x201  SensorECU   alive=0x01 hb=3023
18:16:53.635  0x201  SensorECU   alive=0x01 hb=3024
...
```

- **0x201 (SensorECU)**: 정상 100ms 주기 송신 ✅
- **0x100 (DriveECU)**: 버스에 전혀 없음 ❌

→ **DriveECU가 CAN TX를 전혀 하지 않는 상태.**

#### 3단계 — 0x7E0 프레임 버스 도달 확인

테스트 스크립트로 확인한 결과:

- **SensorECU UART**: `[RX] ID:0x7E0 DLC:8 Data:02 10 02 00` 수신 확인 ✅
- **DriveECU UART**: 아무 로그 없음 ❌

→ PC 송신 프레임은 버스에 도달하지만 DriveECU가 수신하지 못함.

#### 4단계 — ST-LINK 디버거로 MCU 내부 상태 확인

CubeIDE 디버거로 DriveECU를 연결해 Expressions 창에서 각 변수 추적.

**확인한 값들:**

| 표현식 | 값 | 의미 |
|---|---|---|
| `hcan1.State` | `HAL_CAN_STATE_LISTENING` | CAN 초기화 정상 |
| `hcan1.ErrorCode` | `0` | HAL 오류 없음 |
| `((CAN_TypeDef *)0x40006400)->ESR` | `0` | Bus-Off 아님 |
| `htim2.State` | `HAL_TIM_STATE_BUSY` | TIM2 시작됨 |
| `*((unsigned int *)0xE000E100)` | `0x10300000` | NVIC bit28(TIM2) 활성화 |

→ CAN, TIM2, NVIC 모두 정상으로 보였으나 TIM2 콜백 브레이크포인트가 걸리지 않음.

#### 5단계 — 인터럽트 벡터 추적

Resume 후 Suspend로 강제 정지했을 때 콜스택:

```
0x8000d18                           ← 현재 실행 위치 (부트로더 영역)
<signal handler called> 0xfffffff9  ← 인터럽트 발생
main() at main.c:137  0x80408fc    ← 인터럽트 발생 전 위치
```

**`0x8000d18`은 부트로더 Flash 영역(0x08000000~0x08008000)이다.**

TIM2 인터럽트가 발생했는데 DriveECU의 `TIM2_IRQHandler` 대신 부트로더 코드로 점프함.
→ **VTOR(벡터 테이블 주소 레지스터)가 부트로더(0x08000000)를 가리키고 있음.**

---

### 근본 원인

**VTOR이 올바르게 설정되지 않아 모든 인터럽트가 부트로더의 벡터 테이블을 사용한 것.**

정상적인 부팅 경로:
```
MCU 리셋
  → 부트로더 실행 (0x08000000)
  → 앱으로 점프
  → 앱 startup 코드 실행 (Reset_Handler)
  → SystemInit() 호출 → SCB->VTOR = 0x08040000 설정
  → main() 실행
```

CubeIDE 디버거 경로 (문제):
```
MCU 리셋
  → 디버거가 main()으로 직접 PC 설정 ← startup 코드 건너뜀
  → SystemInit() 미실행 → VTOR = 0x00000000 (기본값)
  → 0x00000000은 메모리 맵에서 0x08000000(부트로더)으로 aliasing
  → TIM2/CAN 인터럽트 → 부트로더 벡터 → 부트로더 코드 실행
  → DriveECU의 TIM2 콜백, CAN ISR 모두 미실행
```

이로 인해:
- TIM2 콜백 미실행 → `HAL_CAN_AddTxMessage` 미호출 → 0x100 프레임 미전송
- CAN RX 인터럽트 미실행 → UDS 메시지 수신 불가
- LD2 미깜빡임

> **참고:** 디버거 없는 일반 부팅 시에도 `system_stm32f4xx.c`의 `VECT_TAB_OFFSET`이 빌드 대상 슬롯과 다르면 동일 문제가 발생한다.

---

### 해결

**`main()` 시작부에 VTOR 명시적 설정 추가**

`DriveECU/Core/Src/main.c`:
```c
int main(void)
{
  /* USER CODE BEGIN 1 */
  SCB->VTOR = 0x08040000U;   /* Slot B 링커 스크립트 ORIGIN과 일치 */
  /* USER CODE END 1 */
  ...
```

디버거가 startup 코드를 건너뛰더라도 `main()` 진입 직후 VTOR을 올바르게 설정한다.

**슬롯별 올바른 VTOR 값:**

| 빌드 대상 | 링커 스크립트 ORIGIN | VTOR 값 |
|---|---|---|
| Slot A | `0x08010000` | `0x08010000U` |
| Slot B | `0x08040000` | `0x08040000U` |

**`system_stm32f4xx.c` VECT_TAB_OFFSET도 빌드 대상과 일치시켜야 한다:**

| 빌드 대상 | VECT_TAB_OFFSET |
|---|---|
| Slot A | `0x00010000U` |
| Slot B | `0x00040000U` |

---

### 해결 확인

수정 후 빌드 → 플래시 → 실행:

- **LD2 깜빡임 확인** ✅ → TIM2 콜백 정상 실행, `HAL_CAN_AddTxMessage` HAL_OK 반환
- **`can_monitor.py` 출력:**

```
0x100  DriveECU  DE AD BE EF 00 00 00 00  cnt:0
0x100  DriveECU  DE AD BE EF 00 00 00 01  cnt:1
0x201  SensorECU alive=0x01 hb=3025
...
```

DriveECU 0x100 프레임 200ms 주기 정상 송신 ✅

---

### 디버깅 시 주의사항

**IWDG와 디버거 브레이크포인트:**

IWDG 타임아웃(8초) 동안 브레이크포인트에서 멈춰있으면 MCU가 리셋된다.
디버깅 시 `MX_IWDG_Init()` 임시 주석 처리 후 디버깅 완료 후 반드시 복원.

```c
// MX_IWDG_Init();   /* 디버깅 시 임시 비활성화 — 완료 후 복원 필수 */
```

**Step Over vs Resume:**

CAN RX 인터럽트가 활성화된 이후 구간에서 Step Over(F6)를 사용하면 인터럽트가 끼어들어 GDB 추적이 실패한다. 브레이크포인트 + Resume(F8) 방식을 사용할 것.

---

### 관련 파일

| 파일 | 역할 |
|---|---|
| `DriveECU/Core/Src/main.c` | VTOR 설정 추가 위치 (`USER CODE BEGIN 1`) |
| `DriveECU/Core/Src/system_stm32f4xx.c` | `VECT_TAB_OFFSET` 설정 (슬롯별 값 다름) |
| `DriveECU/STM32F446RETX_FLASH.ld` | Slot A 링커 스크립트 (ORIGIN=0x8010000) |
| `DriveECU/STM32F446RETX_FLASH_SlotB.ld` | Slot B 링커 스크립트 (ORIGIN=0x8040000) |
| `DriveECU/Core/Src/isotp.c` | ISO-TP 수신/송신 레이어 |
| `DriveECU/Core/Src/uds.c` | UDS 서비스 핸들러 (0x10/0x27/0x34/0x36/0x37) |
| `DriveECU/Core/Src/ota_flash.c` | Slot B Flash 소거/쓰기, 메타데이터 기록 |
| `tools/ota_client.py` | PC Python UDS 클라이언트 |

---

### 참고: UDS OTA 정상 동작 시 예상 로그

**ota_client.py 출력:**
```
[OTA] Firmware: DriveECU/Debug/DriveECU_slotB.bin  (25672 bytes)
[UDS] DiagnosticSessionControl(Extended)
[UDS] SecurityAccess - seed request
[UDS]   Seed = 0xXXXXXXXX
[UDS]   Key  = 0xXXXXXXXX
[UDS] Unlocked
[UDS] RequestDownload  addr=0x08040000  size=25672 bytes
[UDS] TransferData: 101 chunks x 256 bytes
[UDS]  100%  block=101
[UDS] RequestTransferExit
[UDS] Transfer complete — ECU will reboot to Slot B
```

**DriveECU UART 출력:**
```
[BL] Bootloader v1.0
[BL] active_slot=1  A=0xAAAAAAAA  B=0xBBBBBBBB
[BL] Slot B selected
[BL] Jump to 0x08040000 ...
[DriveECU v2] Start
```
