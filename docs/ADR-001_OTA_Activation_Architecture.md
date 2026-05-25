# ADR-001: OTA 펌웨어 활성화 아키텍처 결정

| 항목 | 내용 |
|---|---|
| 문서 ID | ADR-001 |
| 상태 | 확정 |
| 작성일 | 2026-05-25 |
| 관련 요구사항 | FR-DRV-007, FR-DRV-008, FR-CICD-007, NFR-SAFE-001 |

---

## 1. 배경

본 프로젝트는 Raspberry Pi 5(OTA Gateway)가 STM32F446RE DriveECU에 CAN/UDS over ISO-TP로 펌웨어를 전송하는 구조다. 초기 구현에서는 Jenkins 파이프라인이 git push를 감지하는 즉시 OTA 전송을 시작했다. 이 과정에서 다음 문제가 발견되었다.

- `RequestDownload(0x34)` 처리 중 Flash Erase가 발생한다 (약 4초 블로킹)
- Flash Erase 동안 CPU가 점유되어 `drive_update()`가 실행되지 않는다
- ECU가 주행 중이었다면 모터 PWM은 마지막 값으로 유지되나 장애물 감지·상태 전이가 불가능하다

이 문제를 해결하기 위해 세 가지 접근을 검토했다.

---

## 2. 검토한 옵션

### 옵션 A: FreeRTOS 도입

Drive task(고우선순위)와 OTA task(저우선순위)를 분리하여 CPU를 번갈아 할당한다.

**한계**

STM32F446RE는 Single-Bank Flash 구조다. Flash Erase 중에는 Flash에서 명령어 fetch가 불가능하므로, FreeRTOS 스케줄러 자체도 Flash에 있으면 실행이 멈춘다. Drive task가 고우선순위여도 Flash Erase 블로킹은 피할 수 없다. TransferData 구간에서 스케줄링 규칙성이 개선되는 효과는 있으나, 핵심 문제(Erase 블로킹)는 해결되지 않는다.

**결론**: 복잡도는 증가하나 근본 문제 해결 불가 → 기각

---

### 옵션 B: ECU 측 지연 활성화 (g_fw_pending, 1차 구현)

`TransferExit(0x37)` 완료 후 즉시 재부팅하지 않고 `g_fw_pending=1` 플래그를 세트한다. ECU가 `DRIVE_IDLE` 상태에 진입할 때 플래그를 확인하여 재부팅한다.

```
RPi5 → OTA 즉시 전송 → ECU: g_fw_pending=1 → DRIVE_IDLE 진입 시 재부팅
```

이는 Uptane 표준의 ECU 측 지연 활성화(deferred activation) 개념과 일치한다.

**한계**

- Flash Erase는 여전히 주행 중에 발생할 수 있다
- `g_fw_pending` 감지 전 사용자가 버튼을 누르면 주행이 시작된 뒤 재부팅이 일어난다
- 활성화 타이밍 제어 로직이 ECU 코드에 존재하여 복잡도가 증가한다

**결론**: 재부팅 타이밍은 해결하나 Flash Erase 구간의 안전 문제는 미해결 → 보류

---

### 옵션 C: RPi5 측 IDLE 감지 후 OTA 시작 (최종 채택)

RPi5가 CAN 0x100 heartbeat를 모니터링하여 ECU의 `driving_state == 0`(IDLE)을 확인한 뒤 OTA 전송을 시작한다.

```
RPi5: heartbeat 수신 → driving_state 확인
       └─ 주행 중 → 대기 (최대 120초)
       └─ IDLE    → UDS OTA 전송 시작
ECU: RequestDownload → Flash Erase (IDLE이므로 안전) → TransferData → TransferExit → 즉시 재부팅
```

---

## 3. 실무 아키텍처와의 비교

실제 차량 OTA는 다음 두 단계로 엄격히 분리된다.

| 단계 | 실무 | 본 프로젝트 |
|---|---|---|
| **다운로드** | 서버 → TCU 내부 저장소(eMMC), 주행 중 가능 | Jenkins 빌드 결과물이 RPi5 파일시스템에 존재 |
| **설치/활성화** | TCU가 안전 조건(정차, IGN OFF 등) 확인 후 ECU에 CAN 플래시 | RPi5가 IDLE 확인 후 CAN으로 ECU 플래시 |

본 프로젝트는 TCU 전용 저장소가 없어 RPi5가 Gateway와 TCU 역할을 겸한다. 그러나 **"안전 조건 확인 후 설치"라는 핵심 원칙은 실무와 동일하게 구현**된다. ECU 측 지연 활성화(옵션 B)보다 Gateway 측 조건 제어(옵션 C)가 실무 TCU 구조에 더 가깝다.

---

## 4. 최종 결정: 옵션 C 채택

### 근거

| 판단 기준 | 옵션 A (FreeRTOS) | 옵션 B (g_fw_pending) | 옵션 C (RPi5 IDLE 감지) |
|---|:---:|:---:|:---:|
| Flash Erase 중 안전 | ✗ | ✗ | ✓ |
| ECU 코드 단순성 | ✗ | ✗ | ✓ |
| 실무 TCU 구조 일치 | △ | △ | ✓ |
| 구현 공수 | 높음 | 낮음 | 낮음 |

### 구현 내용

**RPi5 `ota_client.py`**
- `wait_for_idle()` 메서드: CAN 0x100 heartbeat의 `data[2]`(driving_state) == 0 확인
- `--idle-timeout` 인자: 기본 120초, 0이면 즉시 진행

**DriveECU `uds.c`**
- `RequestDownload(0x34)`: Flash Erase 전 `g_ota_active=1`, 완료 후 `g_ota_active=0`
- Default session 복귀 시 `g_ota_active=0` 클리어
- `RequestTransferExit(0x37)`: 즉시 `NVIC_SystemReset()`

**DriveECU `drive.c`**
- `g_fw_pending` 감지 블록 제거
- `g_ota_active=1` 구간에서 `motor_stop()` + `DRIVE_IDLE` 복귀 (기존 로직 유지)

**DriveECU `uds.h`**
- `g_fw_pending` extern 선언 제거

### g_ota_active 역할

RPi5가 IDLE을 확인하고 OTA를 시작했더라도, OTA 세션 중 사용자가 B1 버튼을 눌러 주행을 시작하는 상황을 방지한다. Flash Erase 구간(`g_ota_active=1`)에서는 `drive_update()`가 즉시 `motor_stop()`을 호출하고 `DRIVE_IDLE`로 강제 복귀한다.

```
[정상 흐름]
RPi5: IDLE 확인 → OTA 시작
ECU:  g_ota_active=1 (Erase) → g_ota_active=0 (TransferData) → 재부팅

[예외 흐름: TransferData 중 버튼 입력]
사용자: B1 버튼 누름
ECU:  g_ota_active=0이므로 DRIVE_RUNNING 진입 가능
      → TransferData 청크 사이 drive_update() 실행됨
      → 주행 가능 (TransferData는 짧은 Flash Write이므로 블로킹 없음)
```

TransferData 구간에서 주행이 재개되는 것은 허용 가능한 동작이다. Flash Erase와 달리 청크당 Flash Write는 수 ms 이내로 완료되며 `drive_update()` 실행 주기에 영향을 주지 않는다.

---

## 5. 제약 사항 및 한계

1. **RPi5 heartbeat 의존**: ECU가 heartbeat를 전송하지 않으면 IDLE 감지가 불가능하다. heartbeat 전송 주기(200ms)와 타임아웃(120초)이 적절히 설정되어야 한다.
2. **120초 타임아웃**: 차량이 계속 주행 중이면 OTA가 중단된다. 실무에서는 주차 조건이 보장되므로 이 문제가 발생하지 않지만, 데모 환경에서는 수동으로 IDLE 상태를 만들어야 한다.
3. **Single-Bank Flash 한계**: Flash Erase 중 CPU 블로킹은 하드웨어 제약으로, 본 아키텍처에서는 IDLE 조건 확인으로 회피한다. RAMFUNC 기반 비동기 Erase(`HAL_FLASHEx_Erase_IT`)는 구현 복잡도 대비 데모 효과가 낮아 채택하지 않았다.

---

## 6. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-05-25 | 최초 작성 — 옵션 A/B/C 검토 및 옵션 C 채택 결정 기록 |
