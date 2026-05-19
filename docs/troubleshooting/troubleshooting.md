# Troubleshooting Log

## Phase 4 — DriveECU 부트로더 연동 후 CAN 인터럽트 완전 불능

**날짜:** 2026-05-18 ~ 2026-05-19
**상태:** ✅ 해결됨
**증상:** DriveECU CAN TX 미발생, LD2 미깜빡임, OTA 타임아웃

---

### 초기 현상

```
python tools/ota_client.py --channel /dev/tty.usbmodem2069368D39451 DriveECU/Debug/DriveECU_slotB.bin

[OTA] Firmware: DriveECU/Debug/DriveECU_slotB.bin  (25672 bytes)
[UDS] DiagnosticSessionControl(Extended)
[OTA] ERROR: Receive timeout
```

- LD2 LED 미깜빡임 → CAN TX 없음
- UART `[DriveECU v2] Start` 출력됨 → `main()`은 실행 중
- CubeIDE Run 포함 **모든 경로에서 CAN 통신 불가**

---

### 진단 과정

#### 1단계 — 포트 혼동

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
```

- **0x201 (SensorECU)**: 정상 100ms 주기 송신 ✅
- **0x100 (DriveECU)**: 버스에 전혀 없음 ❌

#### 3단계 — ST-LINK 디버거로 MCU 내부 상태 확인

CubeIDE Expressions 창에서 확인한 값:

| 표현식 | 값 | 의미 |
|---|---|---|
| `hcan1.State` | `HAL_CAN_STATE_LISTENING` | CAN 초기화 정상 |
| `hcan1.ErrorCode` | `0` | HAL 오류 없음 |
| `((CAN_TypeDef *)0x40006400)->ESR` | `0` | Bus-Off 아님 |
| `htim2.State` | `HAL_TIM_STATE_BUSY` | TIM2 시작됨 |
| `*((unsigned int *)0xE000E100)` | `0x10300000` | NVIC bit28(TIM2) 활성화 |

CAN, TIM2, NVIC 모두 정상으로 보였으나 TIM2 콜백 브레이크포인트가 걸리지 않음.

#### 4단계 — 인터럽트 벡터 추적

Resume 후 Suspend로 강제 정지했을 때 콜스택:

```
0x8000d18                           ← 현재 실행 위치 (부트로더 영역!)
<signal handler called> 0xfffffff9  ← 인터럽트 발생
main() at main.c:137  0x80408fc    ← 인터럽트 발생 전 위치 (Slot B 영역)
```

TIM2 인터럽트가 발생했는데 DriveECU의 `TIM2_IRQHandler` 대신 부트로더 코드(0x08000D18)로 점프.
→ **VTOR이 부트로더 주소(0x08000000)를 가리키고 있음.**

---

### 근본 원인 (총 3개, 순서대로 발견)

#### 원인 1 — VTOR 미설정 (CubeIDE 디버거가 startup 건너뜀)

CubeIDE 디버거가 `main()`으로 직접 PC를 설정할 때 `Reset_Handler → SystemInit()`을 건너뛴다. `SystemInit()`이 실행되지 않으면 `SCB->VTOR`이 기본값(0x00000000)에 머물고, 이는 메모리 맵에서 0x08000000(부트로더)으로 aliasing된다.

```
디버거 직접 점프 경로:
  MCU 리셋 → 부트로더 → 앱 Reset_Handler → SystemInit() → main()
                                              ↑ 디버거가 여기를 건너뜀
  → VTOR = 0x00000000 (= 부트로더 벡터 테이블)
  → 모든 인터럽트 → 부트로더 핸들러 실행
```

이 시점에 CubeIDE는 **Slot B 링커 스크립트**(`STM32F446RETX_FLASH_SlotB.ld`, ORIGIN=0x8040000)로 빌드된 바이너리를 Slot B에 플래시하고 있었다. (콜스택의 `main() at 0x80408fc`가 Slot B 주소 범위임으로 확인)

**수정:** `main()` USER CODE BEGIN 1에서 VTOR 명시적 설정 (Slot B 빌드 기준)
```c
SCB->VTOR = 0x08040000U;   /* Slot B 링커 스크립트 ORIGIN과 일치 */
```

수정 후 CubeIDE Run에서 LD2 깜빡임 및 `can_monitor.py`로 0x100 프레임 확인 ✅

---

#### 중간 과정 — Slot B → Slot A 빌드 전환

VTOR 수정 후 리셋 버튼을 누르면 여전히 CAN이 동작하지 않았다. 원인을 추적하기 위해 UART를 확인하니 두 줄의 Start 로그가 출력됨:

```
[BL] No metadata, defaulting to Slot A
[BL] Jump to 0x08010000 ...
[DriveECU] Start       ← Slot A의 구버전 펌웨어 (Phase 4 이전 빌드)
[DriveECU v2] Start    ← CubeIDE가 Slot B에 플래시한 새 펌웨어
```

이를 통해 CubeIDE가 Slot B(0x08040000)에 새 바이너리를 플래시하고 있었고, 리셋 시 부트로더는 메타데이터가 없어 Slot A(구버전)로 기본 부팅하고 있었음을 파악.

**전환 작업:**
- CubeIDE 링커 스크립트를 `STM32F446RETX_FLASH.ld` (ORIGIN=0x8010000, Slot A)로 변경
- `system_stm32f4xx.c`: `VECT_TAB_OFFSET = 0x00010000U` 로 변경
- `SCB->VTOR = 0x08010000U` 로 변경
- 빌드 → 플래시 → Slot A에 새 펌웨어 적재 완료

---

#### 원인 2 — IWDG 리셋 루프 (디버깅 중 MX_IWDG_Init 주석 처리)

Slot A 전환 후 리셋 시 부팅 루프 발생:

```
[BL] Bootloader v1.0
[BL] No metadata, defaulting to Slot A
[BL] Jump to 0x08010000  SP=0x20020000  PC=0x08011ADD
[DriveECU v2] Start
[BL] Bootloader v1.0
[BL] No metadata, defaulting to Slot A
...   (반복)
```

디버깅 편의를 위해 `MX_IWDG_Init()`을 주석 처리했는데, 부트로더가 IWDG를 시작하고 앱으로 점프하므로 앱이 IWDG를 kick하지 않아 8초 후 리셋이 반복됨.

`hiwdg.Instance`가 NULL 상태라 `HAL_IWDG_Refresh(&hiwdg)`가 실제 IWDG 레지스터를 kick하지 못한 것.

**수정:** `MX_IWDG_Init()` 복원
```c
MX_IWDG_Init();   /* 주석 제거 */
```

---

#### 원인 3 — 부트로더의 `__disable_irq()` 미복원 (최종 원인)

IWDG 수정 후 CubeIDE Run은 정상 동작했지만, 리셋 버튼을 누르면 LD2가 꺼지고 CAN이 동작하지 않음. UART 출력은 정상:

```
[BL] Bootloader v1.0
[BL] No metadata, defaulting to Slot A
[BL] Jump to 0x08010000  SP=0x20020000  PC=0x08011B15
[DriveECU v2] Start            ← 출력은 되는데 이후 CAN/TIM2 동작 없음
```

부트로더 소스를 확인하니:

```c
/* Bootloader/Core/Src/bootloader.c */
static void jump_to_app(uint32_t addr)
{
    __disable_irq();   /* HAL_DeInit() 중 인터럽트 방지 목적 */
    HAL_DeInit();
    SCB->VTOR = addr;
    __set_MSP(sp);
    reset_handler();
}
```

`__disable_irq()`로 PRIMASK=1이 된 상태에서 앱으로 점프. `startup_stm32f446retx.s`의 `Reset_Handler`에는 `__enable_irq()` 호출이 없어 `main()`까지 PRIMASK=1이 유지됨.

```
부트로더 __disable_irq()
  → PRIMASK = 1
  → 앱 Reset_Handler → SystemInit() → main()  (인터럽트 재활성화 없음)
  → printf() 동작  ← UART HAL_UART_Transmit은 폴링 방식이라 PRIMASK 무관
  → HAL_TIM_Base_Start_IT() 설정됨
  → TIM2 인터럽트 발생 시도 → PRIMASK=1 → 차단
  → TIM2 콜백 미실행 → LD2 꺼짐, CAN TX 없음
```

CubeIDE Run이 동작한 이유: 디버거가 `main()`으로 직접 점프할 때 MCU 리셋 상태(PRIMASK=0)에서 시작하므로 인터럽트가 차단되지 않음.

**수정:** `main()` USER CODE BEGIN 1에서 인터럽트 재활성화

```c
/* USER CODE BEGIN 1 */
__enable_irq();           /* 부트로더 __disable_irq() 후 점프하므로 재활성화 */
SCB->VTOR = 0x08010000U;
/* USER CODE END 1 */
```

---

### 최종 수정 코드

`DriveECU/Core/Src/main.c`:
```c
int main(void)
{
  /* USER CODE BEGIN 1 */
  __enable_irq();           /* 부트로더 __disable_irq() 복원 */
  SCB->VTOR = 0x08010000U; /* 디버거 startup 건너뜀 대비 VTOR 명시 설정 */
  /* USER CODE END 1 */

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_CAN1_Init();
  MX_IWDG_Init();           /* 복원 필수 — 부트로더 IWDG kick 이어받음 */
  ...
```

**슬롯별 VTOR / VECT_TAB_OFFSET 대응:**

| 빌드 대상 | 링커 스크립트 | SCB->VTOR | VECT_TAB_OFFSET |
|---|---|---|---|
| Slot A (개발/디버그) | `STM32F446RETX_FLASH.ld` | `0x08010000U` | `0x00010000U` |
| Slot B (OTA 업로드용) | `STM32F446RETX_FLASH_SlotB.ld` | `0x08040000U` | `0x00040000U` |

---

### 해결 확인

리셋 버튼 후 정상 동작:

```
[BL] Bootloader v1.0
[BL] No metadata, defaulting to Slot A
[BL] Jump to 0x08010000  SP=0x20020000  PC=0x08011B15
[DriveECU v2] Start
```

- **LD2 깜빡임** ✅ → TIM2 콜백 정상 실행, PRIMASK=0 확인
- **`can_monitor.py` 출력:**

```
0x100  DriveECU  DE AD BE EF 00 00 00 00  cnt:0
0x100  DriveECU  DE AD BE EF 00 00 00 01  cnt:1
0x201  SensorECU alive=0x01 hb=3025
```

---

### 디버깅 시 주의사항

**IWDG와 브레이크포인트:**
브레이크포인트에서 8초 이상 멈추면 IWDG 만료로 MCU 리셋. 디버깅 시 임시 주석 처리 가능하나 완료 후 반드시 복원.

**Step Over vs Resume:**
CAN RX 인터럽트 활성화 이후 구간에서 Step Over(Fn+F6)하면 인터럽트가 끼어들어 GDB 추적 실패. 브레이크포인트 + Resume(Fn+F8) 방식 사용.

**CubeIDE 링커 스크립트 확인:**
Project → Properties → C/C++ Build → Settings → MCU GCC Linker → Linker Script
Slot A 개발: `STM32F446RETX_FLASH.ld` / Slot B OTA 빌드: `STM32F446RETX_FLASH_SlotB.ld`

---

### 관련 파일

| 파일 | 역할 |
|---|---|
| `DriveECU/Core/Src/main.c` | `__enable_irq()`, `SCB->VTOR`, `MX_IWDG_Init()` 수정 위치 |
| `DriveECU/Core/Src/system_stm32f4xx.c` | `VECT_TAB_OFFSET` (슬롯별 값 다름) |
| `Bootloader/Core/Src/bootloader.c` | `__disable_irq()` + `HAL_DeInit()` 후 점프 |
| `DriveECU/Core/Startup/startup_stm32f446retx.s` | `Reset_Handler` — `__enable_irq()` 없음 확인 |
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
