# Troubleshooting Log

## Phase 4 — DriveECU 부트로더 연동 후 CAN 인터럽트 완전 불능 (ISS-BL-001)

**날짜:** 2026-05-18 ~ 2026-05-19
**상태:** ✅ 해결됨
**증상:** DriveECU CAN TX 미발생, LD2 미깜빡임, OTA 타임아웃

---

### D2. 문제 정의 — 초기 현상

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

### D4. 근본 원인 분석 — 진단 과정


#### 1단계 — CAN 버스 모니터링

`can_monitor.py`로 버스 전체를 모니터링한 결과:

```
18:16:53.535  0x201  SensorECU   alive=0x01 hb=3023
18:16:53.635  0x201  SensorECU   alive=0x01 hb=3024
```

- **0x201 (SensorECU)**: 정상 100ms 주기 송신 ✅
- **0x100 (DriveECU)**: 버스에 전혀 없음 ❌

#### 2단계 — ST-LINK 디버거로 MCU 내부 상태 확인

CubeIDE Expressions 창에서 확인한 값:

| 표현식 | 값 | 의미 |
|---|---|---|
| `hcan1.State` | `HAL_CAN_STATE_LISTENING` | CAN 초기화 정상 |
| `hcan1.ErrorCode` | `0` | HAL 오류 없음 |
| `((CAN_TypeDef *)0x40006400)->ESR` | `0` | Bus-Off 아님 |
| `htim2.State` | `HAL_TIM_STATE_BUSY` | TIM2 시작됨 |
| `*((unsigned int *)0xE000E100)` | `0x10300000` | NVIC bit28(TIM2) 활성화 |

CAN, TIM2, NVIC 모두 정상으로 보였으나 TIM2 콜백 브레이크포인트가 걸리지 않음.

#### 3단계 — 인터럽트 벡터 추적

Resume 후 Suspend로 강제 정지했을 때 콜스택:

```
0x8000d18                           ← 현재 실행 위치 (부트로더 영역!)
<signal handler called> 0xfffffff9  ← 인터럽트 발생
main() at main.c:137  0x80408fc    ← 인터럽트 발생 전 위치 (Slot B 영역)
```

TIM2 인터럽트가 발생했는데 DriveECU의 `TIM2_IRQHandler` 대신 부트로더 코드(0x08000D18)로 점프.
→ **VTOR이 부트로더 주소(0x08000000)를 가리키고 있음.**

---

### D4. 근본 원인 (총 3개, 순서대로 발견)

#### 원인 1 — VTOR 미설정 (디버거 경로에서 부트로더 우회)

> **정정(2026-06-13):** 초기 기록은 "`SystemInit()`이 `VECT_TAB_OFFSET=0`으로 VTOR을 `0x08000000`에 설정"한다고 봤으나 이는 사실이 아니다. `SystemInit()`의 VTOR 설정은 `#if defined(USER_VECT_TAB_ADDRESS)` 안에 있고 이 매크로가 줄곧 주석 처리되어 **컴파일된 적이 없다.** 실제 원인은 *아무도 VTOR을 설정하지 않아* 리셋 기본값에 머문 것이다. 상세는 문서 끝 **재분석(2026-06-13)** 절 참조.

CubeIDE "Debug" 버튼은 앱 ELF를 다운로드한 뒤 PC를 그 ELF 진입점(`ENTRY(Reset_Handler)`)으로 강제한다. 그래서 리셋 벡터(`0x08000000`=부트로더)가 아니라 **앱 `Reset_Handler`부터 실행되고 부트로더를 건너뛴다.**

이 경로에서는 VTOR을 설정하는 주체가 아무도 없다:

```
부트로더 SCB->VTOR = addr   → 부트로더를 건너뛰어 실행 안 됨
앱 main()의 VTOR 설정       → 이 시점엔 아직 없음(60a14f5에서 추가)
앱 SystemInit()의 VTOR 설정 → USER_VECT_TAB_ADDRESS 주석 → 컴파일 안 됨
─────────────────────────────────────────────
∴ VTOR = 리셋 기본값 0x00000000
```

**왜 VTOR=0이면 인터럽트가 부트로더(0x08000000) 핸들러로 가나** — Cortex-M은 인터럽트 핸들러 주소를 `VTOR + (벡터번호 × 4)`에서 읽는다. VTOR=0이면 `0x000000xx`에서 읽는데, STM32F4에서 **`0x00000000`은 메인 플래시 시작(`0x08000000`)의 alias**(BOOT0=0 부팅 시)다. 즉 0번지에 비치는 건 `0x08000000`에 깔린 **부트로더 벡터테이블**이다.

```
TIM2 인터럽트 (TIM2_IRQn=28):
  벡터 오프셋 = (시스템예외 16 + 28) × 4 = 0xB0
  코어가 읽는 주소   0x000000B0
        │ (alias)
        ▼
  실제 읽힌 곳      0x080000B0  = 부트로더 벡터테이블의 TIM2 칸
        ▼
  분기             부트로더 TIM2_IRQHandler (≈ 0x08000D18)
```

따라서 VTOR=`0x00000000`과 `0x08000000`은 이 칩에서 기능적으로 동일하다(같은 플래시 바이트를 가리킴). 물리적 리셋 경로에서는 부트로더가 `SCB->VTOR = addr`로 올바르게 설정하고 점프하므로 이 문제는 나타나지 않는다. 그 경로의 진짜 원인은 아래 **원인 3(PRIMASK)**이다.

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

### D5–D6. 시정 조치 (최종 수정 코드)

`DriveECU/Core/Src/main.c`:
```c
int main(void)
{
  /* USER CODE BEGIN 1 */
  extern uint32_t g_pfnVectors;
  __enable_irq();                              /* 부트로더 __disable_irq() 복원 */
  SCB->VTOR = (uint32_t)&g_pfnVectors;        /* 링커 심볼로 슬롯 무관 자동 설정 */
  /* USER CODE END 1 */

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_CAN1_Init();
  MX_IWDG_Init();           /* 복원 필수 — 부트로더 IWDG kick 이어받음 */
  ...
```

**슬롯별 VTOR / VECT_TAB_OFFSET 대응:**

| 빌드 대상 | 링커 스크립트 | VECT_TAB_OFFSET | SCB->VTOR (g_pfnVectors) |
|---|---|---|---|
| Slot A (개발/디버그) | `STM32F446RETX_FLASH.ld` | `0x00010000U` | 링커가 `0x08010000`으로 배치 |
| Slot B (OTA 업로드용) | `STM32F446RETX_FLASH_SlotB.ld` | `0x00040000U` | 링커가 `0x08040000`으로 배치 |

`g_pfnVectors`는 startup 파일이 `.isr_vector` 섹션 시작에 선언하는 전역 심볼로, 링커가 ORIGIN 주소에 배치한다. 주소를 하드코딩하지 않으므로 Slot A/B 링커 스크립트 전환 시 소스 수정 불필요.

---

## Phase 4.5 — OTA 새 펌웨어 부팅 시 VTOR/링커스크립트 불일치

**날짜:** 2026-05-19
**상태:** ✅ 해결됨
**증상:** OTA 업로드 후 Slot B 부팅 시도 중 CAN 통신 불능 재발

---

### D2. 문제 정의 — 현상 (Phase 4.5)

Phase 4에서 CAN 통신을 복구한 뒤, OTA E2E 테스트를 위해 Slot B 빌드로 전환하는 과정에서 CAN 통신이 다시 망가졌다.

- CubeIDE 링커 스크립트를 `STM32F446RETX_FLASH_SlotB.ld`로 변경
- `system_stm32f4xx.c`의 `VECT_TAB_OFFSET`을 `0x00040000U`로 변경
- `main.c`의 `SCB->VTOR`은 `0x08010000U`로 하드코딩된 상태 유지 → **불일치 발생**

결과적으로 바이너리는 Slot B(0x08040000)에 링크되었으나 VTOR는 Slot A(0x08010000)를 가리켜 인터럽트 벡터가 엉뚱한 위치를 참조함. 수정을 반복하다 혼란이 가중되어 Slot A로 롤백했다.

---

### D4. 근본 원인 — 슬롯 주소 하드코딩

`SCB->VTOR = 0x08010000U` 방식은 Slot A 주소를 소스에 직접 기록하므로, 링커 스크립트만 Slot B로 바꾸면 VTOR와 실제 바이너리 배치 주소가 불일치한다. 슬롯을 전환할 때마다 세 군데(링커 스크립트, VECT_TAB_OFFSET, SCB->VTOR)를 동시에 맞춰야 하므로 실수가 발생하기 쉽다.

```
Slot B 링커 스크립트 변경 시 수정이 필요한 위치:
  1. CubeIDE Linker Script     STM32F446RETX_FLASH_SlotB.ld
  2. system_stm32f4xx.c        VECT_TAB_OFFSET = 0x00040000U
  3. main.c                    SCB->VTOR = 0x08040000U   ← 자주 누락됨
```

---

### D5–D6. 시정 조치 — `g_pfnVectors` 링커 심볼 활용

하드코딩 대신 startup 파일의 벡터 테이블 심볼 주소를 직접 사용.

```c
/* DriveECU/Core/Src/main.c  USER CODE BEGIN 1 */
extern uint32_t g_pfnVectors;
__enable_irq();
SCB->VTOR = (uint32_t)&g_pfnVectors;
```

`g_pfnVectors`는 `startup_stm32f446retx.s`에서 `.isr_vector` 섹션 첫 주소로 선언된 심볼이다. 링커가 해당 섹션을 링커 스크립트의 ORIGIN에 배치하므로, 슬롯을 전환해도 소스 코드를 건드릴 필요 없이 VTOR가 자동으로 맞는 주소를 얻는다.

**OTA 빌드 절차 (이 수정 이후):**
1. Slot A 빌드: `STM32F446RETX_FLASH.ld` + `VECT_TAB_OFFSET=0x00010000U` → MCU 플래시 (현재 실행)
2. Slot B 빌드: `STM32F446RETX_FLASH_SlotB.ld` + `VECT_TAB_OFFSET=0x00040000U` → `.bin` 파일만 추출
3. `ota_client.py`로 Slot B `.bin` 전송
4. 부트로더 재부팅 → Slot B 선택 → VTOR 자동 `0x08040000` 설정 → 정상 동작

---

### D5–D6. 검증 (해결 확인)

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

### D7. 재발 방지 (디버깅 시 주의사항)

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

---

## 재분석 (2026-06-13) — VTOR·PRIMASK는 서로 다른 경로의 단일 원인

Phase 4 D4는 VTOR·PRIMASK를 한 증상(CAN 불통)의 공존 원인으로 묶었으나, 코드와 git 이력을 다시 분석한 결과 **두 원인은 서로 다른 실행 경로에 속하며 실제 부팅에서 공존한 적이 없다.** 8D D4(근본원인)의 재규명에 해당한다.

### 검증된 사실 (코드·git 근거)

| 사실 | 근거 |
|------|------|
| `SystemInit()`은 VTOR을 설정한 적이 없다 | `system_stm32f4xx.c`의 `SCB->VTOR=...`이 `#if defined(USER_VECT_TAB_ADDRESS)` 안에 있고, 이 매크로가 `/* #define ... */`로 Phase 4 직전(`60a14f5^`)부터 현재까지 주석 → 미컴파일 |
| 부트로더 `SCB->VTOR = addr`는 Phase 3부터 존재 | `2a3211f` |
| 앱 `main()`의 VTOR 설정은 Phase 4 수정에서 처음 추가 | `60a14f5` (그 전엔 앱이 VTOR을 어디서도 설정 안 함) |
| 앱 `__enable_irq()`도 같은 수정에서 추가 | `926ea68` |
| CubeIDE Debug는 ELF 진입점(`ENTRY(Reset_Handler)`)으로 PC를 강제 → 부트로더 우회 | 링커 `ENTRY(Reset_Handler)` |

→ 초기 D4 "원인 1"의 메커니즘(`SystemInit`이 `VECT_TAB_OFFSET=0`으로 VTOR을 설정)은 성립하지 않는다. 빌드에서 VTOR을 설정하는 주체는 **부트로더의 `SCB->VTOR = addr` 하나뿐**이었다.

### 경로별 단일 원인

| 경로 | VTOR | PRIMASK | 실제 원인 |
|------|------|---------|-----------|
| **CubeIDE Debug** (부트로더 우회) | 아무도 설정 안 함 → 리셋 기본값 `0x00000000` (= `0x08000000` alias) ❌ | `0` (정상) | **VTOR** — 인터럽트가 부트로더 핸들러로 점프(TIM2→0x8000D18) |
| **물리 리셋 → 부트로더 → 앱** | 부트로더가 `0x08010000`로 설정 → 정상 ✅ | 부트로더 `__disable_irq()` → `1` ❌ | **PRIMASK** — 인터럽트 전역 차단, CAN/TIM2 콜백 미실행 |

- 디버거 경로에서 PRIMASK=0인 이유: 부트로더를 우회해 `__disable_irq()`가 실행되지 않음 → 인터럽트가 *발생은 하되* 틀린 벡터(부트로더 영역)로 점프 = VTOR 증상. (`VTOR=0`이 `0x08000000`을 가리키는 메커니즘은 위 원인 1 참조)
- 실제 부팅 경로에서 VTOR이 정상인 이유: 부트로더가 점프 직전 VTOR을 설정하고, 이후 `Reset_Handler`·`SystemInit()`이 VTOR을 건드리지 않아 유지됨 → 남는 원인은 PRIMASK.

### 결론

VTOR과 PRIMASK는 **동시에 존재한 적이 없다.** 디버거 경로의 단일 원인은 VTOR, 실제 부팅 경로의 단일 원인은 PRIMASK다. 현재 코드는 앱이 `SCB->VTOR = (uint32_t)&g_pfnVectors`와 `__enable_irq()`를 모두 수행하므로 두 경로 모두에서 정상 동작한다(최종 시정 조치는 유효).

검증 방법: 부트로더 프로젝트를 디버그(`Bootloader-driveECU.launch`)하고 앱 심볼을 함께 로드해 리셋→부트로더→앱을 한 흐름으로 추적하면서 `*(uint32_t*)0xE000ED08`(SCB->VTOR)을 관찰하면 위 표가 실증된다.
