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

## 6. 후속 검토 (2026-06-04): 근본 원인 해결책 — RAMFUNC / Dual-bank

옵션 C 채택 후, "Flash Erase 4초 블로킹"이라는 **근본 원인 자체를 없애는** 두 접근을 추가 검토했다. 옵션 C는 이 블로킹을 *해결*한 것이 아니라 *정차 시점으로 회피*한 것이므로, 아래를 향후 개선 경로로 명시한다. **결론적으로 single-bank F446 + 데모 범위에서는 옵션 C 결정을 유지한다.**

### 옵션 D: RAMFUNC (Erase 루틴 + 최소 안전감시자를 RAM에서 실행)

Erase 중 CPU가 멈추는 이유는 BUSY 상태의 플래시에서 명령어를 fetch할 수 없기 때문이다. erase 드라이버·워치독 피딩·모터컷 가드를 RAM에 상주(`__RAM_FUNC`)시키면 Erase 중에도 그 코드는 실행된다.

- **한계**: single-bank는 Erase 중 플래시 *전체*가 잠긴다. 살리려는 코드의 호출체인·ISR·벡터테이블(VTOR 재배치)·상수(`.rodata`)까지 **모두 RAM**이어야 한다. 주행 제어 전체(CAN RX→장애물→모터)를 살리려면 사실상 앱 대부분이 RAM으로 가고, ISO 26262 freedom-from-interference 입증 부담이 커진다. → 현실적 용도는 **"Erase 구간 최소 안전상태(모터 OFF) 유지"** 까지.
- **실무**: 플래시 드라이버를 RAM에서 실행하는 것은 Flash Bootloader(FBL)의 표준 관행이다(UDS로 플래시 루틴 blob을 RAM에 다운로드 후 RoutineControl로 실행). 단, **앱 전체를 살리는 용도로는 쓰지 않는다.**

### 옵션 E: Dual-bank Flash (Read-While-Write 하드웨어)

뱅크별 독립 제어로, 한 뱅크를 erase/write하는 동안 다른 뱅크에서 코드를 실행(RWW)한다. **주행 중 백그라운드 다운로드 + 안전 시점 활성화**가 가능해지는, AUTOSAR Adaptive UCM이 전제하는 구조이자 "주행 중 업데이트" ECU의 실제 모습이다.

- **한계**: STM32F446은 single-bank 전용 — 하드웨어 교체(STM32F76x/H7/L4 등) 필요. 본 프로젝트 범위 밖.

### 결정에의 영향

- single-bank F446 + 데모에서는 **옵션 C 유지**가 합리적이다(OTA를 정차 구간에 한정 → 안전기능과 시간적으로 분리 → ISO 26262 안전 입증이 단순).
- **deferred activation(옵션 B, `g_fw_pending`) 개념 자체는 틀리지 않았다.** ADR-001이 B를 기각한 진짜 이유는 "single-bank에서 *Erase*를 안전하게 못 만든다"였지 "활성화를 미루는 것이 나쁘다"가 아니다. 옵션 D/E로 Erase가 안전해지면 deferred activation은 다시 올바른 설계가 된다.
- **보강(별개 과제)**: ECU가 주행 중 `RequestDownload(0x34)`를 **NRC 0x22(conditionsNotCorrect)** 로 거부하는 ECU 측 전제조건 강제 — 게이트웨이 신뢰 의존을 줄인다(ISO 24089 installation preconditions, Uptane SR-UP-004 "게이트웨이를 신뢰하지 말라").

> **문서 정합성 메모**: 본 ADR(옵션 C)이 정본이다. SRS FR-DRV-008·FR-CICD-007, TEST_SPEC TC-OTA-007/008, RTM-001, diagram.md의 상태/시퀀스 다이어그램을 옵션 C(IDLE 확인은 Gateway `wait_for_idle`, TransferExit 시 ECU 즉시 재부팅)로 **정합화 완료(2026-06-13)**. 옛 옵션 B(`g_fw_pending`) 서술은 본 ADR §2-B의 기각 기록으로만 보존한다.

---

## 7. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-05-25 | 최초 작성 — 옵션 A/B/C 검토 및 옵션 C 채택 결정 기록 |
| 1.1 | 2026-06-04 | §6 추가 — 근본 원인 해결책(RAMFUNC/dual-bank) 검토, 옵션 C 유지 재확인, deferred activation 부활 조건·ECU 전제조건 강제·문서 정합성 메모 |
| 1.2 | 2026-06-13 | §6 정합성 메모 갱신 — SRS/TEST_SPEC/RTM/diagram을 옵션 C(Gateway `wait_for_idle` + TransferExit 즉시 재부팅)로 정합화 완료 반영 |
