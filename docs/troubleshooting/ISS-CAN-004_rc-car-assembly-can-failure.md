# ISS-CAN-004: RC카 조립 후 CAN 통신 불가

## 개요

| 항목 | 내용 |
|------|------|
| 발생 단계 | Phase 6 (RC카 통합 조립) |
| 발생 시점 | 아크릴판 설치 및 ECU/RPi5/브레드보드 재배치 후 |
| 증상 | DriveECU CAN 초기화 실패, candump 수신 없음 |
| 상태 | 해결 완료 |

---

## D2. 문제 정의 — 증상

### DriveECU UART 로그
```
[BL] Bootloader v1.0
[BL] No metadata, defaulting to Slot A
[BL] Jump to 0x08010000  SP=0x20020000  PC=0x080122A1
[CAN] ConfigFilter=0
[CAN] Start=1 state=5 ESR=0x00000000
[CAN] Notify=1
[DriveECU v1] Start, Slot=0
[TIM2] CAN TX fail: state=5 ESR=0x00000000 err=0x00060000
[ALIVE] t=3000 CAN state=5 ESR=0x00000000
```

### 오류 코드 해석
| 값 | 의미 |
|----|------|
| `Start=1` | HAL_CAN_Start() → HAL_ERROR |
| `state=5` | HAL_CAN_STATE_ERROR |
| `ESR=0x00000000` | 에러 플래그 없음 (버스 오프 아님) |
| `err=0x00060000` | HAL_CAN_ERROR_NOT_INITIALIZED \| HAL_CAN_ERROR_TIMEOUT |

### RPi5 candump
```bash
$ candump can0
# 아무 메시지도 수신되지 않음 (터미널 정지 상태)
```

---

## 배경

- RC카 조립 전(브레드보드 단독 테스트) 시점에는 CAN 통신 정상 동작 확인
- 아크릴판 2층 구조로 ECU 2개, RPi5, 브레드보드 재배치 후 CAN 불통 발생
- 재배치 과정에서 모든 배선을 탈거 후 재연결

---

## D4. 근본 원인 분석 — 조사 과정

### 1단계: 소프트웨어 확인

**펌웨어 버전 확인**
- DriveECU가 구버전(v2) 실행 중 확인 → Phase 6A(v1) 재플래시
- `[DriveECU v2] Start` 하드코딩 버그 발견 → APP_VERSION 사용하도록 수정
- 재플래시 후에도 `CAN Start=1` 동일하게 발생 → 소프트웨어 문제 아님

**SensorECU 펌웨어 확인**
- 부트로더 미플래시 상태 확인 → 부트로더 플래시 후 Phase 6B 정상 실행
- `[SensorECU v1] Start, Slot=0` 확인

### 2단계: 트랜시버 핀 전압 측정

| 핀 | 측정값 | 판정 |
|----|--------|------|
| VCC | 3.3V | 정상 |
| CAN TX (CTX) | 3.3V | 정상 (Idle/Recessive) |
| CAN RX (CRX) | 3.3V | 정상 (Recessive) |
| CANH | 2.2V | 정상 범위 (Recessive) |
| CANL | 2.2V | 정상 범위 (Recessive) |
| RS | 미노출 (모듈 내부 GND 연결) | 정상 |

CANH = CANL = 2.2V → 차동전압 0V → Recessive 상태 (정상)

### 3단계: 트랜시버 교체 테스트
- DriveECU와 SensorECU 트랜시버 상호 교체
- 문제가 트랜시버를 따라가지 않음 → 트랜시버 불량 아님

### 4단계: CAN 핀 배선 오류 발견 및 수정

아크릴판 재배치 전 촬영한 사진을 확인하고 CubeMX 핀맵과 대조한 결과, CAN 핀 배선 오류를 발견했다.

**오류 내용**: 재배치 과정에서 트랜시버를 PB8(CRX), PB9(CTX)에 연결했으나, 실제 CubeMX 설정은 PA11(CAN1_RX), PA12(CAN1_TX)였다.

- `stm32f4xx_hal_msp.c` (DriveECU, SensorECU 동일):
  ```
  PA11 ------> CAN1_RX
  PA12 ------> CAN1_TX
  ```
- STM32F446RE는 CAN1을 PA11/PA12 또는 PB8/PB9 두 곳에 매핑 가능하지만, 이 프로젝트는 PA11/PA12로 설정됨
- PB8/PB9는 CAN alternate function이 활성화되지 않은 일반 GPIO 상태였으므로 트랜시버-MCU 간 CAN 신호 연결이 전혀 이뤄지지 않았음
- HAL_CAN_Start() 타임아웃은 INAK 비트가 클리어되지 않아 발생 (RX 라인이 CAN 컨트롤러에 도달하지 못하므로 버스 동기화 불가)

**수정**: 트랜시버 CRX → PA11, CTX → PA12 로 재연결 후 CAN 통신 정상 동작 확인.

---

## CAN 배선 체크리스트

### DriveECU ↔ SN65HVD230 (트랜시버 A)
- [x] PA12 → CTX
- [x] PA11 → CRX
- [x] 3.3V → VCC
- [x] GND → GND

### SensorECU ↔ SN65HVD230 (트랜시버 B)
- [x] PA12 → CTX
- [x] PA11 → CRX
- [x] 3.3V → VCC
- [x] GND → GND

### CAN 버스
- [ ] 트랜시버 A CANH ─── 트랜시버 B CANH ─── CANable CANH
- [ ] 트랜시버 A CANL ─── 트랜시버 B CANL ─── CANable CANL

### 종단저항
- [ ] 버스 한쪽 끝: CANH ─ 120Ω ─ CANL
- [ ] 버스 반대쪽 끝: CANH ─ 120Ω ─ CANL

### CAN 버스
- [x] 트랜시버 A CANH ─── 트랜시버 B CANH ─── CANable CANH
- [x] 트랜시버 A CANL ─── 트랜시버 B CANL ─── CANable CANL

### 종단저항
- [x] 버스 한쪽 끝: CANH ─ 120Ω ─ CANL
- [x] 버스 반대쪽 끝: CANH ─ 120Ω ─ CANL

### 공통 GND
- [x] DriveECU GND, SensorECU GND, 브레드보드 GND, AA배터리 GND 모두 공통 연결

---

## D4. 근본 원인

재배치 과정에서 트랜시버 CRX/CTX를 PB8/PB9에 연결했으나, CubeMX 설정은 PA11/PA12였다.

PB8/PB9는 CAN alternate function이 비활성화된 일반 GPIO 상태이므로 STM32 CAN 컨트롤러가 트랜시버와 연결되지 않았다. 버스 동기화(11 consecutive recessive bits)가 불가능하여 INAK 비트가 클리어되지 않고 HAL_CAN_Start()가 10ms 타임아웃으로 실패했다.

**핵심 교훈**: CubeMX 핀맵과 실제 배선을 항상 대조할 것. 조립 전후 배선 사진 촬영이 디버깅에 유효하다.

---

## D5–D6. 시정 조치 & 검증

트랜시버 CRX → PA11, CTX → PA12 로 재연결. CAN 통신 정상 동작 확인.
