# PRIMASK / VTOR 원인 격리 실험

## 목적

Phase 4, Phase 5B 트러블슈팅에서 CAN 인터럽트 불통의 원인으로
`__disable_irq()` 미복원(PRIMASK=1)과 VTOR 오설정이 기록되어 있다.

이 문서는 코드 수정 후 동작 확인에 그치지 않고, 두 요인이 실제로
원인이었음을 **체계적인 실험으로 입증**하는 과정을 기록한다.

---

## 배경

### Phase 4, Phase 5B에서 기록된 원인

| 원인 | 설명 |
|------|------|
| VTOR 오설정 | `VECT_TAB_OFFSET=0x00000000`으로 인터럽트 벡터가 부트로더 영역을 가리킴 |
| PRIMASK=1 | 부트로더 `__disable_irq()` 후 점프, 앱 `Reset_Handler`에 `__enable_irq()` 없음 |

두 원인이 **동시에** 존재했기 때문에 PRIMASK=1이 VTOR 오류 증상까지 은폐했다.

### 기존 진단의 한계

Phase 4 당시 진단은 ST-LINK 디버거 콜스택과 Expressions 창으로 수행했다.
원인을 수정하고 동작이 복구됨으로써 결론을 내렸으나,
각 요인을 **단독으로 격리하여 검증하지는 않았다**.

---

## 실험 설계: 2-factor 격리 행렬

두 요인을 독립적으로 조작하여 각각의 인과관계를 확인한다.

| 실험 | VTOR | PRIMASK | 예측 증상 | 검증 대상 |
|------|------|---------|-----------|-----------|
| T1 | 부트로더 주소(`0x08000000`) | 0 (정상) | TIM2가 부트로더 핸들러로 점프 → 오동작 | VTOR가 인터럽트 경로를 결정함 |
| T2 | 올바른 앱 주소 | 1 (마스크됨) | TIM2 콜백 미실행, CAN TX 없음 | PRIMASK=1이 인터럽트를 전역 차단함 |
| T3 | 올바른 앱 주소 | 0 (정상) | 정상 동작 | 두 수정이 모두 필요함 |
| T4 | 부트로더 주소 | 1 | T2와 동일 (PRIMASK가 VTOR 오류를 은폐) | 두 원인이 동시 존재 시 PRIMASK가 VTOR 증상을 가림 |

T4가 Phase 4/5B에서 실제로 발생한 상태다.

---

## 실험 방법론

### 관측 → 주입 2단계 원칙

코드 수정 없이 레지스터 값을 직접 읽고 변경하여 원인을 특정한다.

1. **관측**: 문제 재현 상태에서 레지스터 값이 가설과 일치하는지 확인
2. **주입**: 디버거로 레지스터 값을 실시간 변경해 증상이 사라지는지 확인

재컴파일 없이 원인을 격리할 수 있다.

### 사용 도구

| 도구 | 용도 |
|------|------|
| UART printf | PRIMASK 값을 MCU 진입 시점에 직접 출력 |
| CubeIDE Debug Attach | 실행 중인 MCU에 디버거를 붙여 레지스터 상태 읽기 |
| GDB Registers 뷰 | PRIMASK, VTOR 레지스터 값 확인 및 주입 |
| `can_monitor.py` | CAN TX 복구 여부 확인 |

---

## 실험 A: PRIMASK 격리 (T2 검증)

### 준비: `__enable_irq()` 임시 제거

`DriveECU/Core/Src/main.c` `USER CODE BEGIN 1`:

```c
/* USER CODE BEGIN 1 */
extern uint32_t g_pfnVectors;
// __enable_irq();   ← 주석 처리 (실험용)
SCB->VTOR = (uint32_t)&g_pfnVectors;
/* USER CODE END 1 */
```

`USER CODE BEGIN 2` (UART 초기화 이후):

```c
/* USER CODE BEGIN 2 */
printf("[APP] PRIMASK at entry = %lu\r\n", __get_PRIMASK());
volatile int hold = 1;
while (hold);   // 디버거 연결 대기용 스핀 루프
```

`printf`를 `USER CODE BEGIN 1`에 두면 `MX_USART2_UART_Init()` 이전이라
출력이 되지 않는다. UART 초기화 이후 배치가 필수다.

### Phase 1 — UART 관측

**목적:** PRIMASK=1이 부트로더에서 앱으로 실제로 전달되는지 확인.

**절차:**
1. 위 코드로 빌드
2. STM32CubeProgrammer로 플래시 (CubeIDE Debug 버튼 사용 금지 — 부트로더 우회)
3. 메타데이터 섹터(`0x08008000`) Erase (ECDSA 검증 우회)
4. 물리 리셋 버튼 → 시리얼 모니터 확인

**결과:**
```
[BL] Bootloader v1.0
[BL] No metadata, defaulting to Slot A
[BL] Jump to 0x08010000  SP=0x20020000  PC=0x08012581
[APP] PRIMASK at entry = 1
```

**해석:**
- 부트로더 `__disable_irq()` → PRIMASK=1 → 앱 진입 시에도 PRIMASK=1 유지 확인
- **가설 관측 완료**: 부트로더가 PRIMASK=1 상태 그대로 점프한다는 사실 입증

### Phase 2 — 디버거 주입

**목적:** PRIMASK=0으로 변경하면 CAN이 살아나는지 확인.

**절차:**
1. 물리 리셋 → `while(hold)` 스핀 중인 상태
2. CubeIDE Debug Configurations → Debugger 탭 → Reset behaviour: **None**
3. Startup 탭 → Download: **false**
4. Debug 버튼 (MCU 리셋 없이 연결)
5. Registers 뷰 → PRIMASK = **1** 확인 (관측)
6. PRIMASK 값을 **0**으로 변경 (주입)
7. Expressions 창 → `hold` = **0** (스핀 루프 탈출)
8. Resume → UART CAN RX 로그 출력 확인

**결과:**
```
[RX] ID:0x201 DLC:8 ...   ← PRIMASK=0 주입 직후 CAN RX 인터럽트 즉시 동작
```

PRIMASK 레지스터 하나만 변경했을 때 CAN RX가 즉시 복구됨.
코드 수정, 재플래시, 하드웨어 변경 없이 레지스터 주입만으로 재현.

---

## 실험 B: VTOR 격리 (T1 검증)

### 준비: `SCB->VTOR` 설정 제거

`DriveECU/Core/Src/main.c` `USER CODE BEGIN 1`에서 VTOR 설정을 주석 처리:

```c
/* USER CODE BEGIN 1 */
extern uint32_t g_pfnVectors;
__enable_irq();
// SCB->VTOR = (uint32_t)&g_pfnVectors;  ← 주석 처리 (실험용)
/* USER CODE END 1 */
```

PRIMASK는 정상(`__enable_irq()` 활성), VTOR만 앱이 설정하지 않은 상태.
`system_stm32f4xx.c`의 `SystemInit()`은 `VECT_TAB_OFFSET=0x00010000`으로
`SCB->VTOR = 0x08010000`을 설정하지만, 부트로더 `HAL_DeInit()`이 이를 초기화하고
앱에서 재설정이 없으면 부트로더 벡터 테이블(`0x08000000`)을 가리킨 채로 동작한다.

### Phase 1 — 디버거 콜스택 관측

**절차:** 물리 리셋 → CubeIDE Debug Attach (Reset behaviour: None) → Suspend

**결과 — Expressions 창 VTOR 값:**

```
SCB->VTOR = 0  (= 0x00000000, 부트로더 HAL_DeInit 이후 초기화됨)
```

**결과 — 인터럽트 발생 시 콜스택:**

```
0x8003088    <signal handler called>() at 0xfffffff9
uds_process() at uds.c:202    0x80124c6
main() at main.c:169           0x8010cfa
```

- `0x8003088` = `0x08003088` — **부트로더 주소 영역(0x08000000~0x0800FFFF)**
- 인터럽트가 발생할 때 앱 핸들러가 아닌 부트로더 벡터 테이블의 핸들러로 점프됨
- **가설 관측 완료**: VTOR이 부트로더 영역을 가리키고 있을 때 인터럽트가 오동작한다는 사실 입증

### Phase 2 — 디버거 주입

**목적:** VTOR을 올바른 앱 주소로 변경하면 정상 동작하는지 확인.

**절차:**
1. 물리 리셋 → Debug Attach → Suspend
2. Expressions 창에서 `SCB->VTOR` = `134283264` 입력 (= `0x08010000`, Slot A 시작 주소)
3. Resume → UART 및 CAN 동작 확인

**결과:**
```
[RX] ID:0x201 DLC:8 ...   ← VTOR=0x08010000 주입 직후 CAN RX 인터럽트 즉시 정상 동작
```

VTOR 레지스터 하나만 올바른 앱 주소로 변경했을 때 인터럽트가 즉시 복구됨.
코드 수정, 재플래시 없이 레지스터 주입만으로 재현.

---

## 결론

### PRIMASK 인과관계 3조건 충족

| 조건 | 확인 방법 | 결과 |
|------|-----------|------|
| 상관관계 — PRIMASK=1일 때 CAN 불통 | UART 관측 (`PRIMASK at entry = 1`) | ✅ |
| 시간 순서 — PRIMASK=1이 먼저, CAN 불통이 나중 | 부트로더 `__disable_irq()` → 앱 진입 순서 | ✅ |
| 단독 원인 — PRIMASK만 바꿨을 때 CAN 복구 | 디버거 레지스터 주입 실험 | ✅ |

### VTOR 인과관계 3조건 충족

| 조건 | 확인 방법 | 결과 |
|------|-----------|------|
| 상관관계 — VTOR=부트로더 주소일 때 인터럽트 오동작 | 콜스택 `0x08003088` (부트로더 영역) 확인 | ✅ |
| 시간 순서 — VTOR 오설정이 먼저, 인터럽트 오동작이 나중 | 부트로더 `HAL_DeInit()` → 앱 진입 순서 | ✅ |
| 단독 원인 — VTOR만 바꿨을 때 인터럽트 복구 | 디버거 VTOR=`0x08010000` 주입 실험 | ✅ |

**두 원인이 각각 독립적으로 인과관계가 입증됐다.**

Phase 4/5B에서 두 원인이 동시에 존재했던 상태(T4)는 PRIMASK=1이 전역 인터럽트를 차단하여
VTOR 오설정에 의한 오동작 증상까지 은폐하는 구조였다.

**`__disable_irq()` 미복원 및 VTOR 미복원이 CAN 인터럽트 불통의 양대 원인임을
레지스터 주입 실험으로 각각 확정.**

### Terminate 후 MCU 동작 확인

Debug Attach 상태에서 PRIMASK=0 주입 후 CubeIDE **Terminate** 버튼을 눌렀을 때:

- UART CAN 로그가 끊기지 않고 계속 출력됨
- MCU가 리셋되지 않고 주입된 PRIMASK=0 상태 그대로 실행을 이어감

**Terminate는 디버거 프로세스만 종료하고 MCU를 리셋하지 않는다.**
디버거가 분리된 후에도 MCU는 마지막 레지스터 상태를 유지한 채 실행된다.

디버거 종료 방식별 동작:

| 방식 | MCU 상태 |
|------|----------|
| **Terminate** | 리셋 없음, 마지막 레지스터 상태로 실행 계속 |
| **Stop (세션 종료)** | MCU 리셋 |
| **Resume 후 Terminate** | 실행 중 상태 그대로 디버거만 분리 |

---

### CubeIDE 직접 Debug 실행과의 차이 확인

CubeIDE Debug 버튼(기본 설정)으로 실행하면 PRIMASK=0으로 시작한다.
디버거가 MCU를 리셋하고 부트로더를 거치지 않고 앱 `Reset_Handler`로
직접 점프하기 때문이다. 이 경로에서는 `__disable_irq()`가 호출되지 않아
문제가 재현되지 않는다.

```
CubeIDE Debug 직접 실행    물리 리셋 + Debug Attach
        ↓                           ↓
  부트로더 우회               부트로더 실행
  PRIMASK = 0 (기본값)        __disable_irq()
  CAN 정상 동작               PRIMASK = 1
                              CAN 인터럽트 차단
```

이 차이를 모르면 "로컬에서는 되는데 실제 부팅하면 안 된다"는 문제를
디버거로 재현하지 못하고 원인을 놓치게 된다.

---

## 환경 구성 중 발생한 문제들

### 문제 1 — `arm-none-eabi-gdb` 실행 불가

macOS quarantine 플래그로 인해 CubeIDE가 내장 GDB 실행을 차단.
```bash
sudo xattr -cr /Applications/STM32CubeIDE.app
```
→ [상세: local_cubeide_debug_setup.md](local_cubeide_debug_setup.md)

### 문제 2 — `.cproject` 유실로 "Unsupported build configuration"

`git checkout` 시 이전에 추적됐다가 gitignore된 `.cproject` 삭제됨.
`DriveECU.ioc` → Generate Code로 재생성.
→ [상세: local_cubeide_debug_setup.md](local_cubeide_debug_setup.md)

### 문제 3 — printf가 UART 초기화 전 호출됨

`USER CODE BEGIN 1`은 `HAL_Init()`, `MX_USART2_UART_Init()` 이전이라
printf 출력이 나타나지 않았다. `USER CODE BEGIN 2`로 이동하여 해결.

### 문제 4 — 메타데이터 미삭제로 ECDSA 검증 실패

CubeIDE 직접 플래시는 서명 없는 바이너리를 쓰지만, 이전 OTA에서 기록된
메타데이터(`slot_a_size > 64`)가 남아있어 부트로더가 ECDSA 검증을 수행하고 실패.
STM32CubeProgrammer로 `0x08008000`(16KB) 섹터 Erase 후 해결.

### 문제 5 — `while(hold)` 스핀 중 IWDG 만료로 반복 재부팅

**증상:** Debug Attach 후 디버깅 도중 MCU가 계속 재부팅됨.

**원인 확인 — `RCC->CSR` 레지스터 직접 읽기:**

CubeIDE Expressions 창에서 `RCC->CSR` 읽기:

```
RCC->CSR = 0x3E000003
```

비트 분석:

| 비트 | 플래그 | 값 | 의미 |
|------|--------|----|------|
| 29 | **IWDGRSTF** | **1** | **IWDG 워치독 리셋 발생** |
| 28 | SFTRSTF | 1 | 소프트웨어 리셋 (디버거 연결) |
| 27 | PORRSTF | 1 | 전원 인가 리셋 |
| 26 | PINRSTF | 1 | 물리 리셋 버튼 |
| 25 | BORRSTF | 1 | BOR 리셋 |

IWDGRSTF(bit 29) = 1 → **IWDG 워치독이 재부팅 원인 확정.**

RCC→CSR의 리셋 플래그는 재부팅이 발생해도 클리어되지 않고 누적된다.
`RCC->CSR |= RCC_CSR_RMVF` 로 명시적으로 클리어하기 전까지 유지된다.
따라서 재부팅 직후 이 레지스터를 읽으면 원인을 파악할 수 있다.

**근본 원인:**

```
부트로더 IWDG 시작
        ↓
앱 진입 → MX_IWDG_Init() → IWDG 재설정 (약 8초 타임아웃)
        ↓
while(hold) 스핀 — HAL_IWDG_Refresh() 없음
        ↓
8초 후 IWDG 만료 → MCU 리셋 → 부트로더 재시작
```

**해결:** `while(hold)` 루프 안에서 IWDG kick 추가.

```c
volatile int hold = 1;
while (hold) { HAL_IWDG_Refresh(&hiwdg); }
```

**참고:** IWDG는 ARM Cortex-M의 디버거 halt 상태에서도 카운트를 멈추지 않는다.
WWDG(Window Watchdog)와 달리 IWDG는 디버그 중단점에서 정지해도 계속 동작하므로,
브레이크포인트에서 8초 이상 멈추면 동일하게 리셋된다.

---

## 실험 완료 후 코드 복원

실험용으로 수정한 코드를 원래 상태로 복원한다.

**`DriveECU/Core/Src/main.c`**
```c
/* USER CODE BEGIN 1 */
extern uint32_t g_pfnVectors;
__enable_irq();           /* 복원 */
SCB->VTOR = (uint32_t)&g_pfnVectors;
/* USER CODE END 1 */
```

- `USER CODE BEGIN 2`의 `printf("[APP] PRIMASK...")`, `while(hold)` 제거
- `MX_IWDG_Init()` 주석 해제
- `HAL_IWDG_Refresh(&hiwdg)` 주석 해제

**`Bootloader/Core/Src/bootloader.c`**
- IWDG init 주석 해제

---

## 진행 결과

| 단계 | 상태 | 결과 |
|------|------|------|
| 실험 환경 구성 | ✅ 완료 | quarantine 제거, .cproject 재생성, 메타데이터 삭제 |
| Phase 1 UART 관측 | ✅ 완료 | `PRIMASK at entry = 1` 확인 |
| Phase 2 디버거 주입 | ✅ 완료 | PRIMASK=0 주입 → CAN RX 즉시 복구 확인 |
| 실험 B (VTOR 격리) | ✅ 완료 | 콜스택 `0x08003088`(부트로더 영역) 확인, VTOR=`0x08010000` 주입 → 정상 복구 |

---

## 참고

- [phase4_driveECU_bootloader_can_interrupt.md](phase4_driveECU_bootloader_can_interrupt.md) — 원인 발견 이력
- [phase5_slot_b_can_failure.md](phase5_slot_b_can_failure.md) — Phase 5B 동일 원인 재발
- [local_cubeide_debug_setup.md](local_cubeide_debug_setup.md) — 환경 구성 트러블슈팅
