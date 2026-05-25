# 소프트웨어 테스트 명세서
## Automotive Secure OTA ECU System

---

| 항목 | 내용 |
|------|------|
| 문서 번호 | TS-OTA-001 |
| 버전 | v1.0 |
| 상태 | Draft |
| 작성일 | 2026-05-25 |
| 작성자 | JaehyeongIm |
| 참조 표준 | ASPICE SWE.4/5/6, ISO 26262-6, ISO 14229-1 (UDS) |

---

## 개정 이력

| 버전 | 날짜 | 변경 내용 | 작성자 |
|------|------|----------|--------|
| v1.0 | 2026-05-25 | 초안 작성 | JaehyeongIm |

---

## 목차

1. [개요](#1-개요)
2. [참조 문서](#2-참조-문서)
3. [용어 및 약어](#3-용어-및-약어)
4. [테스트 전략](#4-테스트-전략)
5. [테스트 환경](#5-테스트-환경)
6. [테스트 케이스 — CAN 통신](#6-테스트-케이스--can-통신-tc-can)
7. [테스트 케이스 — 주행 제어](#7-테스트-케이스--주행-제어-tc-drv)
8. [테스트 케이스 — OTA 프로토콜](#8-테스트-케이스--ota-프로토콜-tc-ota)
9. [테스트 케이스 — 보안](#9-테스트-케이스--보안-tc-sec)
10. [테스트 케이스 — 슬롯 관리](#10-테스트-케이스--슬롯-관리-tc-slot)
11. [테스트 케이스 — CI/CD 파이프라인](#11-테스트-케이스--cicd-파이프라인-tc-ci)
12. [요구사항 추적성 매트릭스](#12-요구사항-추적성-매트릭스)
13. [테스트 결과 요약](#13-테스트-결과-요약)

---

## 1. 개요

### 1.1 목적

본 문서는 Automotive Secure OTA ECU 시스템의 소프트웨어 테스트 명세를 정의한다.
ASPICE SWE.4(소프트웨어 단위 검증), SWE.5(소프트웨어 통합 테스트), SWE.6(소프트웨어 적격성 테스트)
레벨의 테스트 항목을 포함하며, ISO 26262-6 소프트웨어 레벨 안전 요구사항을 참조하여 작성되었다.

### 1.2 테스트 범위

| 대상 | 설명 |
|------|------|
| DriveECU | STM32F446RE, 주행 제어 + OTA 수신 |
| SensorECU | STM32F446RE, HC-SR04 거리 측정 + CAN 전송 |
| OTA Gateway | Raspberry Pi 5, Jenkins CI/CD + UDS 클라이언트 |
| Bootloader | ECDSA-P256 서명 검증 + A/B 슬롯 관리 |

### 1.3 테스트 범위 외

- Bootloader 자체 단위 테스트 (별도 명세 예정)
- 하드웨어 내구성 시험 (온도, 진동)
- EMC/EMI 시험

---

## 2. 참조 문서

| 문서 번호 | 제목 |
|----------|------|
| SRS-001 | CAN Secure OTA Pipeline 소프트웨어 요구사항 명세서 v1.4 |
| DIAG-001 | 시스템 다이어그램 (docs/diagram.md) |
| ISS-CAN-004 | CAN 핀 배선 오류 해결 기록 |
| ISO 14229-1 | Unified Diagnostic Services (UDS) |
| ISO 26262-6 | Functional Safety — Software Level |
| ASPICE v3.1 | SWE.4 / SWE.5 / SWE.6 |

---

## 3. 용어 및 약어

| 약어 | 설명 |
|------|------|
| ECU | Electronic Control Unit |
| OTA | Over-The-Air (무선 펌웨어 업데이트) |
| UDS | Unified Diagnostic Services (ISO 14229) |
| ISO-TP | ISO 15765-2 Transport Protocol |
| ECDSA | Elliptic Curve Digital Signature Algorithm |
| NRC | Negative Response Code |
| SID | Service Identifier (UDS) |
| DUT | Device Under Test |
| TC | Test Case |
| PASS | 테스트 통과 |
| FAIL | 테스트 실패 |
| N/T | Not Tested (미수행) |
| BLOCK | 선행 조건 미충족으로 수행 불가 |

---

## 4. 테스트 전략

### 4.1 테스트 레벨

```
┌─────────────────────────────────────────┐
│  SWE.6  소프트웨어 적격성 테스트           │  ← E2E: git push → OTA 완료
├─────────────────────────────────────────┤
│  SWE.5  소프트웨어 통합 테스트             │  ← ECU 간 CAN 통신, OTA 세션
├─────────────────────────────────────────┤
│  SWE.4  소프트웨어 단위 검증               │  ← 개별 모듈 기능 검증
└─────────────────────────────────────────┘
```

### 4.2 테스트 우선순위

| 우선순위 | 기준 |
|---------|------|
| High (H) | 안전 관련 기능, OTA 핵심 경로 |
| Medium (M) | 정상 동작 기능 |
| Low (L) | 부가 기능, 로그 출력 |

### 4.3 합격 기준 (Pass Criteria)

- High 우선순위 TC: **100% PASS** 필수
- Medium 우선순위 TC: **90% 이상 PASS**
- Low 우선순위 TC: **80% 이상 PASS**
- 미구현 기능으로 인한 FAIL은 별도 이슈로 등록 후 관리

---

## 5. 테스트 환경

### 5.1 하드웨어 구성

| 구성요소 | 모델 | 역할 |
|---------|------|------|
| DriveECU | STM32F446RE Nucleo-64 | DUT (주행 제어 + OTA) |
| SensorECU | STM32F446RE Nucleo-64 | DUT (거리 측정 + CAN) |
| OTA Gateway | Raspberry Pi 5 (8GB) | Jenkins + UDS 클라이언트 |
| CAN 인터페이스 | CANable v2.0 (USB-CAN) | PC ↔ CAN 버스 |
| CAN 트랜시버 | SN65HVD230 × 2 | CAN 물리 계층 |
| 거리 센서 | HC-SR04 | 장애물 감지 |
| 모터 드라이버 | TB6612FNG | DC 모터 PWM 제어 |
| 터미네이션 저항 | 120Ω × 2 | CAN 버스 종단 |

### 5.2 소프트웨어 구성

| 구성요소 | 버전 |
|---------|------|
| STM32CubeIDE | 최신 |
| arm-none-eabi-gcc | 12.x |
| python-can | 4.x |
| cryptography (Python) | 41.x |
| Jenkins | 2.x (RPi5) |
| candump / cansend | can-utils |

### 5.3 CAN 버스 구성

```
DriveECU (PA11/PA12)
    └── SN65HVD230 ── CANH/CANL ── SN65HVD230 ── SensorECU (PA11/PA12)
                                         │
                                    CANable (USB)
                                         │
                                    RPi5 / PC
```

### 5.4 메모리 맵 (DriveECU / SensorECU 동일)

| 영역 | 주소 | 크기 | 내용 |
|------|------|------|------|
| Bootloader | 0x08000000 | 32KB | ECDSA 검증 + 슬롯 선택 |
| Metadata | 0x08008000 | 16KB | A/B 슬롯 상태 |
| Slot A | 0x08010000 | 192KB | 애플리케이션 A |
| Slot B | 0x08040000 | 256KB | 애플리케이션 B |

---

## 6. 테스트 케이스 — CAN 통신 (TC-CAN)

### TC-CAN-001 DriveECU 하트비트 주기 검증

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-CAN-001 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §3.1 |
| **목적** | DriveECU가 CAN ID 0x100으로 200ms 주기 하트비트를 전송하는지 검증 |
| **사전 조건** | DriveECU 전원 ON, CAN 버스 정상, candump 수신 준비 |
| **테스트 절차** | 1. `candump can0` 실행<br>2. 60초간 ID=0x100 메시지 타임스탬프 수집<br>3. 연속 메시지 간격 계산 |
| **기대 결과** | 메시지 간격 200ms ± 10ms |
| **합격 기준** | 전체 샘플의 95% 이상이 허용 범위 이내 |
| **결과** | N/T |
| **비고** | — |

---

### TC-CAN-002 DriveECU 하트비트 페이로드 검증

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-CAN-002 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §3.1 |
| **목적** | 하트비트 페이로드의 각 필드가 올바르게 설정되는지 검증 |
| **사전 조건** | DriveECU 부팅 완료, APP_VERSION=1, Slot A 부팅 |
| **테스트 절차** | 1. `candump can0` 수신<br>2. ID=0x100 첫 수신 메시지 페이로드 확인<br>&nbsp;&nbsp;- byte[0]: APP_VERSION<br>&nbsp;&nbsp;- byte[1]: active_slot (0=A, 1=B)<br>&nbsp;&nbsp;- byte[2]: driving_state<br>&nbsp;&nbsp;- byte[3]: obstacle_flag |
| **기대 결과** | byte[0]=0x01, byte[1]=0x00, byte[2]=0x00, byte[3]=0x00 |
| **합격 기준** | 모든 필드 기대값 일치 |
| **결과** | N/T |
| **비고** | — |

---

### TC-CAN-003 SensorECU 장애물 메시지 전송 검증

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-CAN-003 |
| **테스트 레벨** | SWE.5 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §3.2 |
| **목적** | 장애물 10cm 이내 진입 시 CAN 0x200 obstacle 플래그 세트 검증 |
| **사전 조건** | SensorECU 전원 ON, CAN 버스 정상 |
| **테스트 절차** | 1. `candump can0`으로 ID=0x200 모니터링<br>2. HC-SR04 앞 50cm에 장애물 배치 → byte[0]=0x00 확인<br>3. 장애물을 10cm 이내로 이동<br>4. byte[0]=0x01 확인<br>5. 장애물 제거 → byte[0]=0x00 복귀 확인 |
| **기대 결과** | 10cm 이내 진입 시 byte[0]=0x01, 이후 byte[1:2]=실측 거리(cm) |
| **합격 기준** | 플래그 세트/클리어 정상 동작 |
| **결과** | N/T |
| **비고** | — |

---

### TC-CAN-004 DriveECU 장애물 플래그 CAN 수신 반영 검증

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-CAN-004 |
| **테스트 레벨** | SWE.5 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §3.3 |
| **목적** | DriveECU가 0x200 수신 후 하트비트 0x100 byte[3]에 obstacle_flag 반영 확인 |
| **사전 조건** | DriveECU + SensorECU 모두 ON, CAN 연결 |
| **테스트 절차** | 1. 장애물 없는 상태에서 0x100 byte[3]=0x00 확인<br>2. 장애물 10cm 이내 배치<br>3. 50ms 이내 0x100 byte[3]=0x01 반영 확인 |
| **기대 결과** | obstacle_flag가 다음 하트비트 주기(200ms) 이내에 반영됨 |
| **합격 기준** | 반영 지연 200ms 이내 |
| **결과** | N/T |
| **비고** | — |

---

## 7. 테스트 케이스 — 주행 제어 (TC-DRV)

### TC-DRV-001 버튼 트리거 — 전진 시작

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-DRV-001 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §4.1 |
| **목적** | B1(USER) 버튼 누름 시 DRIVE_IDLE → DRIVE_RUNNING 전환 및 전진 시작 검증 |
| **사전 조건** | DriveECU 부팅 완료, DRIVE_IDLE 상태, 장애물 없음 |
| **테스트 절차** | 1. UART 모니터 연결<br>2. B1 버튼 1회 누름<br>3. UART 로그 확인: `[DRIVE v1] 출발`<br>4. 차량 전진 여부 육안 확인 |
| **기대 결과** | 버튼 누름 후 300ms 이내 전진 시작, UART 로그 출력 |
| **합격 기준** | 전진 동작 및 로그 출력 확인 |
| **결과** | N/T |
| **비고** | 디바운스 300ms 적용 |

---

### TC-DRV-002 전진 시간 종료 후 자동 정지

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-DRV-002 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | M |
| **참조 요구사항** | SRS-001 §4.1 |
| **목적** | FORWARD_MS(3000ms) 경과 후 자동 정지 및 IDLE 복귀 검증 |
| **사전 조건** | TC-DRV-001 PASS, 장애물 없음 |
| **테스트 절차** | 1. B1 버튼으로 전진 시작<br>2. 스톱워치 측정 시작<br>3. 자동 정지 시 스톱워치 정지<br>4. UART 로그: `[DRIVE] 1m 완료 → 정지` 확인 |
| **기대 결과** | 3000ms ± 50ms 후 정지 |
| **합격 기준** | 정지 시간 허용 오차 이내, IDLE 복귀 확인 |
| **결과** | N/T |
| **비고** | FORWARD_MS 캘리브레이션 후 재검증 권장 |

---

### TC-DRV-003 v1 — 10cm 장애물 감지 즉시 정지

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-DRV-003 |
| **테스트 레벨** | SWE.5 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §4.2 |
| **목적** | APP_VERSION=1에서 10cm 이내 장애물 감지 시 즉시 정지 검증 |
| **사전 조건** | APP_VERSION=1 플래시 완료, DriveECU + SensorECU ON |
| **테스트 절차** | 1. 전방 30cm에 장애물 배치 (정지 안 됨 확인)<br>2. B1 버튼으로 전진 시작<br>3. 전방 10cm 지점에 장애물 고정<br>4. 차량 정지 확인<br>5. UART 로그: `[DRIVE] 장애물(10cm) 감지 → 정지` 확인 |
| **기대 결과** | 장애물 10cm 이내 진입 후 200ms 이내 정지 |
| **합격 기준** | 정지 동작 확인, 로그 출력 확인 |
| **결과** | N/T |
| **비고** | CAN 0x200 수신 → drive_update() 처리 지연 최대 20ms |

---

### TC-DRV-004 v2 — 30cm 감속 시작 검증 (OTA 후)

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-DRV-004 |
| **테스트 레벨** | SWE.5 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §4.3 |
| **목적** | APP_VERSION=2에서 30cm 장애물 진입 시 속도 감소 검증 |
| **사전 조건** | v1→v2 OTA 완료, APP_VERSION=2 부팅 확인 |
| **테스트 절차** | 1. B1 버튼으로 전진 시작<br>2. UART에서 `[DriveECU v2] Start` 확인<br>3. 전방 35cm에서 30cm 구간 통과 시 속도 감소 육안 확인<br>4. 10cm 도달 시 완전 정지 확인 |
| **기대 결과** | 30cm 진입 시 속도 감소, 10cm에서 완전 정지 |
| **합격 기준** | 감속 동작 가시적 확인, 10cm 정지 확인 |
| **결과** | N/T |
| **비고** | 속도 수식: sp = 300 + (300×(dist-10)/20) |

---

### TC-DRV-005 v3 — 정지 후 자동 후진 복귀 (OTA 후)

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-DRV-005 |
| **테스트 레벨** | SWE.5 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §4.4 |
| **목적** | APP_VERSION=3에서 장애물 정지 후 300ms 대기 → 600ms 후진 → IDLE 복귀 검증 |
| **사전 조건** | v2→v3 OTA 완료, APP_VERSION=3 부팅 확인 |
| **테스트 절차** | 1. B1 버튼으로 전진 시작<br>2. 10cm 장애물로 정지 유도<br>3. 정지 후 후진 시작 확인 (UART: `[DRIVE] 후진 시작`)<br>4. 600ms 후진 후 정지 확인 (UART: `[DRIVE] 후진 완료 → 대기`)<br>5. 버튼 재입력 가능 상태(IDLE) 확인 |
| **기대 결과** | 정지→300ms 대기→후진 600ms→IDLE 순서 정상 동작 |
| **합격 기준** | 전체 시퀀스 UART 로그 및 육안 확인 |
| **결과** | N/T |
| **비고** | — |

---

### TC-DRV-006 OTA 다운로드 중 주행 유지

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-DRV-006 |
| **테스트 레벨** | SWE.5 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §5.1 |
| **목적** | OTA TransferData 수신 중 차량이 계속 주행하는지 검증 (Uptane 표준 반영) |
| **사전 조건** | DriveECU + SensorECU ON, Jenkins OTA 준비, 장애물 없음 |
| **테스트 절차** | 1. B1 버튼으로 전진 시작<br>2. 전진 중 Jenkins에서 OTA 파이프라인 수동 트리거<br>3. TransferData 진행 중 차량 주행 여부 육안 확인<br>4. TransferExit 완료 후 UART: `FW ready Slot X — IDLE 시 재부팅` 확인<br>5. 전진 완료(FORWARD_MS 경과) 후 자동 재부팅 확인 |
| **기대 결과** | OTA 다운로드 중 주행 지속, IDLE 시점에 자동 재부팅 |
| **합격 기준** | 다운로드 중 모터 정지 없음(Erase 4초 제외), IDLE 복귀 후 재부팅 |
| **결과** | N/T |
| **비고** | Flash Erase(~4초) 구간은 CPU 점유로 일시 제어 불가 — 별도 주석 처리됨 |

---

## 8. 테스트 케이스 — OTA 프로토콜 (TC-OTA)

### TC-OTA-001 DiagnosticSessionControl — ExtendedSession 정상 진입

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-OTA-001 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §6.1 |
| **목적** | UDS 0x10 0x02 수신 시 ExtendedSession 진입 및 긍정 응답 검증 |
| **사전 조건** | DriveECU ON, CAN 정상 |
| **테스트 절차** | 1. `cansend can0 7E0#02100200000000` 전송<br>2. 0x7E8 수신 확인<br>3. UART 로그: `[UDS] Extended session` 확인 |
| **기대 결과** | 응답: `7E8#03500200000000`, UART 로그 출력 |
| **합격 기준** | 긍정 응답(0x50 0x02) 수신 확인 |
| **결과** | N/T |
| **비고** | ISO 14229-1 §9.2.1 |

---

### TC-OTA-002 SecurityAccess — Seed/Key 정상 해제

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-OTA-002 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §6.2 |
| **목적** | SecurityAccess Seed 요청 → Key 계산 → Unlock 정상 동작 검증 |
| **사전 조건** | TC-OTA-001 PASS (ExtendedSession 상태) |
| **테스트 절차** | 1. 0x27 0x01 (Seed 요청) 전송<br>2. Seed(4byte) 수신<br>3. Key = Seed XOR 0xDEADBEEF 계산<br>4. 0x27 0x02 + Key 전송<br>5. 0x67 0x02 응답 확인 |
| **기대 결과** | 응답: `0x67 0x02` (Unlock OK) |
| **합격 기준** | Unlock 성공 응답 확인 |
| **결과** | N/T |
| **비고** | tools/ota_client.py 참조 |

---

### TC-OTA-003 SecurityAccess — 잘못된 Key NRC 검증

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-OTA-003 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §6.2 |
| **목적** | 잘못된 Key 전송 시 NRC 0x35(invalidKey) 응답 및 세션 상태 복귀 검증 |
| **사전 조건** | TC-OTA-001 PASS (ExtendedSession 상태) |
| **테스트 절차** | 1. Seed 요청 및 수신<br>2. 의도적으로 틀린 Key(0xDEADDEAD) 전송<br>3. NRC 수신 확인<br>4. 이후 RequestDownload 시도 → NRC 0x22 확인 |
| **기대 결과** | NRC: `0x7F 0x27 0x35`, 잠금 상태 유지 |
| **합격 기준** | NRC 코드 정확히 일치 |
| **결과** | N/T |
| **비고** | ISO 14229-1 §9.4.5 |

---

### TC-OTA-004 SecurityAccess 없이 RequestDownload 시도 — NRC

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-OTA-004 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §6.3 |
| **목적** | SecurityAccess 미완료 상태에서 RequestDownload 시 NRC 0x22(conditionsNotCorrect) 검증 |
| **사전 조건** | ExtendedSession 상태, Unlock 미완료 |
| **테스트 절차** | 1. TC-OTA-001로 ExtendedSession 진입<br>2. SecurityAccess 생략<br>3. 0x34 (RequestDownload) 전송<br>4. NRC 수신 확인 |
| **기대 결과** | NRC: `0x7F 0x34 0x22` |
| **합격 기준** | NRC 코드 정확히 일치 |
| **결과** | N/T |
| **비고** | — |

---

### TC-OTA-005 RequestDownload — 비활성 슬롯 자동 선택 검증

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-OTA-005 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §6.3 |
| **목적** | RequestDownload 시 ECU가 자동으로 비활성 슬롯을 OTA 대상으로 선택하는지 검증 |
| **사전 조건** | TC-OTA-002 PASS (Unlocked 상태), Slot A 부팅 중 |
| **테스트 절차** | 1. 0x34 (RequestDownload) 전송<br>2. UART 로그 확인: `[UDS] Target Slot B addr=0x08040000`<br>3. 응답 0x74 수신 확인 |
| **기대 결과** | Slot A 부팅 → Slot B 선택, UART 로그 및 0x74 응답 |
| **합격 기준** | 비활성 슬롯 자동 선택 및 응답 확인 |
| **결과** | N/T |
| **비고** | — |

---

### TC-OTA-006 TransferData — 블록 시퀀스 오류 NRC

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-OTA-006 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | M |
| **참조 요구사항** | SRS-001 §6.4 |
| **목적** | TransferData 블록 시퀀스 번호 불일치 시 NRC 0x73(wrongBlockSequenceCounter) 검증 |
| **사전 조건** | TC-OTA-005 PASS (Downloading 상태) |
| **테스트 절차** | 1. 첫 청크 전송 (bsq=0x01) → 0x76 응답 확인<br>2. 시퀀스 번호 0x03(skip)으로 전송<br>3. NRC 수신 확인 |
| **기대 결과** | NRC: `0x7F 0x36 0x73` |
| **합격 기준** | NRC 코드 정확히 일치 |
| **결과** | N/T |
| **비고** | ISO 14229-1 §14.4.5 |

---

### TC-OTA-007 TransferExit — 메타데이터 기록 및 g_fw_pending 세트

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-OTA-007 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §6.5 |
| **목적** | TransferExit 수신 시 즉시 재부팅 없이 g_fw_pending=1 세트 및 UART 로그 출력 검증 |
| **사전 조건** | TransferData 전체 청크 전송 완료 |
| **테스트 절차** | 1. 0x37 (RequestTransferExit) 전송<br>2. 0x77 응답 확인<br>3. UART 로그: `FW ready Slot X — IDLE 시 재부팅` 확인<br>4. 이 시점에서 차량이 재부팅되지 않음 확인 |
| **기대 결과** | 0x77 응답 수신, 즉시 재부팅 없음, UART 로그 출력 |
| **합격 기준** | 응답 확인, 재부팅 미발생 확인 |
| **결과** | N/T |
| **비고** | Uptane 표준: 활성화 분리 원칙 |

---

### TC-OTA-008 IDLE 상태 진입 시 자동 재부팅 및 슬롯 전환

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-OTA-008 |
| **테스트 레벨** | SWE.5 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §6.5 |
| **목적** | g_fw_pending=1 상태에서 DRIVE_IDLE 진입 시 자동 재부팅 및 새 슬롯 부팅 검증 |
| **사전 조건** | TC-OTA-007 PASS (g_fw_pending=1), DRIVE_RUNNING 또는 DRIVE_STOPPED 상태 |
| **테스트 절차** | 1. 전진 완료 대기(FORWARD_MS 경과) 또는 장애물로 정지 유도<br>2. DRIVE_IDLE 진입 시 UART 로그: `[DRIVE] 새 펌웨어 대기 중 → 재부팅` 확인<br>3. 재부팅 후 부트로더 로그: `[BL] Jump to 0x0804XXXX` 확인<br>4. candump로 CAN 0x100 byte[1] 슬롯 전환 확인 |
| **기대 결과** | IDLE 진입 후 200ms 내 재부팅, 새 슬롯으로 부팅 |
| **합격 기준** | 슬롯 전환 확인, 새 APP_VERSION 하트비트 확인 |
| **결과** | N/T |
| **비고** | — |

---

## 9. 테스트 케이스 — 보안 (TC-SEC)

### TC-SEC-001 ECDSA 서명 검증 통과 — 정상 펌웨어 부팅

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-SEC-001 |
| **테스트 레벨** | SWE.6 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §7.1 |
| **목적** | 올바른 ECDSA-P256 서명이 첨부된 펌웨어가 부트로더 검증을 통과하고 정상 부팅되는지 검증 |
| **사전 조건** | tools/sign_firmware.py로 서명된 펌웨어 준비 |
| **테스트 절차** | 1. 정상 서명 펌웨어로 OTA 수행 (TC-OTA-001~008)<br>2. 재부팅 후 UART 로그 확인<br>3. `[BL] ECDSA OK` 또는 `[BL] Jump to 0x...` 로그 확인<br>4. 애플리케이션 정상 실행 확인 |
| **기대 결과** | ECDSA 검증 성공, 새 슬롯 애플리케이션 실행 |
| **합격 기준** | 부팅 완료 및 정상 CAN 하트비트 전송 확인 |
| **결과** | N/T |
| **비고** | — |

---

### TC-SEC-002 ECDSA 서명 검증 실패 — 변조 펌웨어 차단

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-SEC-002 |
| **테스트 레벨** | SWE.6 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §7.2 |
| **목적** | 서명이 변조된 펌웨어를 부트로더가 거부하고 이전 슬롯을 유지하는지 검증 |
| **사전 조건** | 서명된 펌웨어 바이너리 준비, 이진 에디터로 페이로드 1바이트 변조 |
| **테스트 절차** | 1. 변조된 펌웨어로 OTA 수행<br>2. 재부팅 후 UART 로그 확인<br>3. `[BL] ECDSA FAIL` 로그 및 이전 슬롯 유지 확인<br>4. CAN 0x100 byte[1] 슬롯 변경 없음 확인 |
| **기대 결과** | 검증 실패, 이전 슬롯 유지 |
| **합격 기준** | 변조 펌웨어 차단 및 Fallback 동작 확인 |
| **결과** | N/T |
| **비고** | 공격 시나리오 테스트 |

---

### TC-SEC-003 서명 없는 펌웨어 차단

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-SEC-003 |
| **테스트 레벨** | SWE.6 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §7.2 |
| **목적** | 서명이 첨부되지 않은 원본 .bin 파일을 OTA로 전송했을 때 부트로더가 차단하는지 검증 |
| **사전 조건** | sign_firmware.py 미적용 원본 .bin 준비 |
| **테스트 절차** | 1. 서명 없는 원본 .bin으로 OTA 수행<br>2. 재부팅 후 부트로더 검증 실패 로그 확인<br>3. 이전 슬롯 유지 확인 |
| **기대 결과** | 검증 실패, 이전 슬롯 유지 |
| **합격 기준** | 미서명 펌웨어 차단 확인 |
| **결과** | N/T |
| **비고** | 공격 시나리오 테스트 |

---

### TC-SEC-004 DefaultSession에서 SecurityAccess 시도 — NRC

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-SEC-004 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §6.1 |
| **목적** | DefaultSession 상태에서 SecurityAccess 시도 시 NRC 0x22 응답 검증 |
| **사전 조건** | DriveECU 부팅 직후 (DefaultSession 상태) |
| **테스트 절차** | 1. ExtendedSession 진입 없이 0x27 0x01 전송<br>2. NRC 수신 확인 |
| **기대 결과** | NRC: `0x7F 0x27 0x22` |
| **합격 기준** | NRC 코드 정확히 일치 |
| **결과** | N/T |
| **비고** | — |

---

## 10. 테스트 케이스 — 슬롯 관리 (TC-SLOT)

### TC-SLOT-001 메타데이터 없을 때 Slot A 기본 부팅

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-SLOT-001 |
| **테스트 레벨** | SWE.4 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §5.1 |
| **목적** | 메타데이터 없는 신규 플래시 상태에서 Slot A로 기본 부팅되는지 검증 |
| **사전 조건** | 메타데이터 섹터(0x08008000) 전체 Erase 완료, Slot A 애플리케이션 플래시 완료 |
| **테스트 절차** | 1. 전원 ON 또는 리셋<br>2. UART 로그 확인: `[BL] No metadata, defaulting to Slot A`<br>3. CAN 0x100 byte[1]=0x00 (Slot A) 확인 |
| **기대 결과** | Slot A 부팅 확인 |
| **합격 기준** | 로그 및 CAN 필드 확인 |
| **결과** | N/T |
| **비고** | — |

---

### TC-SLOT-002 A→B 슬롯 전환 검증

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-SLOT-002 |
| **테스트 레벨** | SWE.5 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §5.2 |
| **목적** | Slot A 부팅 상태에서 OTA 후 Slot B로 전환되는지 검증 |
| **사전 조건** | TC-SLOT-001 PASS, Slot A 부팅 중 |
| **테스트 절차** | 1. OTA 수행 (TC-OTA-001~008)<br>2. 재부팅 후 UART: `[BL] Jump to 0x08040000` 확인<br>3. CAN 0x100 byte[1]=0x01 (Slot B) 확인<br>4. 새 APP_VERSION 하트비트 확인 |
| **기대 결과** | Slot B 부팅, 새 버전 동작 |
| **합격 기준** | 슬롯 전환 및 버전 확인 |
| **결과** | N/T |
| **비고** | — |

---

### TC-SLOT-003 B→A 슬롯 재전환 검증

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-SLOT-003 |
| **테스트 레벨** | SWE.5 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §5.2 |
| **목적** | Slot B 부팅 상태에서 OTA 후 Slot A로 재전환 검증 |
| **사전 조건** | TC-SLOT-002 PASS, Slot B 부팅 중 |
| **테스트 절차** | 1. 새 펌웨어(v3) 빌드 후 OTA 수행<br>2. 재부팅 후 Slot A 부팅 확인<br>3. CAN 0x100 byte[1]=0x00 (Slot A) 확인 |
| **기대 결과** | Slot A 부팅 |
| **합격 기준** | 슬롯 전환 확인 |
| **결과** | N/T |
| **비고** | — |

---

## 11. 테스트 케이스 — CI/CD 파이프라인 (TC-CI)

### TC-CI-001 git push → Jenkins 자동 트리거

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-CI-001 |
| **테스트 레벨** | SWE.6 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §8.1 |
| **목적** | 코드 변경 후 git push 시 Jenkins 파이프라인이 자동 실행되는지 검증 |
| **사전 조건** | RPi5 SSH 접속 가능, Jenkins 설치 완료, 파이프라인 Job 구성 완료 |
| **테스트 절차** | 1. DriveECU 소스 파일 임의 수정 후 git push<br>2. Jenkins UI에서 파이프라인 자동 실행 확인<br>3. 빌드 로그: `DriveECU changed: true` 확인 |
| **기대 결과** | push 후 1분 이내 파이프라인 자동 시작 |
| **합격 기준** | 자동 트리거 확인 |
| **결과** | N/T |
| **비고** | RPi5 SSH 복구 후 진행 |

---

### TC-CI-002 변경 ECU만 선택적 빌드 검증

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-CI-002 |
| **테스트 레벨** | SWE.6 |
| **우선순위** | M |
| **참조 요구사항** | SRS-001 §8.2 |
| **목적** | DriveECU만 수정 시 SensorECU 빌드/플래시가 스킵되는지 검증 |
| **사전 조건** | TC-CI-001 PASS |
| **테스트 절차** | 1. DriveECU 파일만 수정 후 push<br>2. Jenkins 로그: `SensorECU changed: false` 확인<br>3. Flash SensorECU 스테이지 skip 확인 |
| **기대 결과** | SensorECU 빌드/플래시 스킵 |
| **합격 기준** | 선택적 빌드 동작 확인 |
| **결과** | N/T |
| **비고** | — |

---

### TC-CI-003 E2E OTA 파이프라인 — v1→v2 전환

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-CI-003 |
| **테스트 레벨** | SWE.6 |
| **우선순위** | H |
| **참조 요구사항** | SRS-001 §8.3 |
| **목적** | git push부터 슬롯 전환 확인까지 전체 OTA 파이프라인 E2E 검증 |
| **사전 조건** | TC-CI-001 PASS, v1 플래시 완료 |
| **테스트 절차** | 1. drive.h에서 APP_VERSION=1→2 변경 후 push<br>2. Jenkins: build → sign → OTA 전송 로그 확인<br>3. DriveECU IDLE 후 재부팅 확인<br>4. CAN 0x100 byte[0]=0x02 (v2), byte[1] 슬롯 전환 확인<br>5. Jenkins: `OTA 완료: SlotX 부팅 확인` 로그 확인 |
| **기대 결과** | git push 후 자동으로 v2 OTA 완료 |
| **합격 기준** | 슬롯 전환 및 APP_VERSION=2 동작 확인 |
| **결과** | N/T |
| **비고** | 전체 소요시간 기록 권장 |

---

### TC-CI-004 OTA 실패 시 파이프라인 실패 처리

| 항목 | 내용 |
|------|------|
| **TC ID** | TC-CI-004 |
| **테스트 레벨** | SWE.6 |
| **우선순위** | M |
| **참조 요구사항** | SRS-001 §8.4 |
| **목적** | OTA 전송 실패 또는 슬롯 전환 미확인 시 Jenkins 파이프라인이 FAILURE로 종료되는지 검증 |
| **사전 조건** | Jenkins 파이프라인 실행 중 |
| **테스트 절차** | 1. OTA 전송 중 CAN 버스 임시 차단 (케이블 분리)<br>2. Jenkins 로그: `OTA 실패: SlotX 기대, SlotY 확인됨` 확인<br>3. Jenkins 파이프라인 FAILURE 상태 확인<br>4. ECU가 이전 펌웨어 유지 확인 |
| **기대 결과** | 파이프라인 FAILURE, ECU 이전 슬롯 유지 |
| **합격 기준** | FAILURE 처리 및 Fallback 동작 확인 |
| **결과** | N/T |
| **비고** | — |

---

## 12. 요구사항 추적성 매트릭스

| 요구사항 ID | 요구사항 요약 | 관련 TC |
|------------|-------------|---------|
| SRS §3.1 | DriveECU CAN 하트비트 0x100, 200ms | TC-CAN-001, TC-CAN-002 |
| SRS §3.2 | SensorECU 장애물 메시지 0x200 | TC-CAN-003, TC-CAN-004 |
| SRS §4.1 | 버튼 트리거 전진 + 시간 정지 | TC-DRV-001, TC-DRV-002 |
| SRS §4.2 | v1 장애물 10cm 즉시 정지 | TC-DRV-003 |
| SRS §4.3 | v2 30cm 감속 + 10cm 정지 | TC-DRV-004 |
| SRS §4.4 | v3 정지 후 자동 후진 복귀 | TC-DRV-005 |
| SRS §5.1 | OTA 중 주행 유지 (Uptane) | TC-DRV-006 |
| SRS §5.2 | A/B 슬롯 전환 | TC-SLOT-001~003 |
| SRS §6.1 | UDS ExtendedSession | TC-OTA-001, TC-SEC-004 |
| SRS §6.2 | SecurityAccess Seed/Key | TC-OTA-002, TC-OTA-003 |
| SRS §6.3 | RequestDownload 비활성 슬롯 | TC-OTA-004, TC-OTA-005 |
| SRS §6.4 | TransferData 블록 순서 검증 | TC-OTA-006 |
| SRS §6.5 | TransferExit + IDLE 활성화 | TC-OTA-007, TC-OTA-008 |
| SRS §7.1 | ECDSA 정상 펌웨어 부팅 | TC-SEC-001 |
| SRS §7.2 | ECDSA 변조/미서명 차단 | TC-SEC-002, TC-SEC-003 |
| SRS §8.1 | Jenkins 자동 트리거 | TC-CI-001 |
| SRS §8.2 | 선택적 ECU 빌드 | TC-CI-002 |
| SRS §8.3 | E2E OTA 파이프라인 | TC-CI-003 |
| SRS §8.4 | OTA 실패 처리 | TC-CI-004 |

---

## 13. 테스트 결과 요약

### 13.1 결과 집계표

| 카테고리 | 전체 TC | PASS | FAIL | BLOCK | N/T |
|---------|--------|------|------|-------|-----|
| TC-CAN | 4 | — | — | — | 4 |
| TC-DRV | 6 | — | — | — | 6 |
| TC-OTA | 8 | — | — | — | 8 |
| TC-SEC | 4 | — | — | — | 4 |
| TC-SLOT | 3 | — | — | — | 3 |
| TC-CI | 4 | — | — | — | 4 |
| **합계** | **29** | **—** | **—** | **—** | **29** |

### 13.2 발견된 결함

| 결함 ID | 관련 TC | 심각도 | 설명 | 상태 |
|---------|--------|--------|------|------|
| — | — | — | 테스트 수행 전 | — |

### 13.3 테스트 완료 기준

- [ ] High 우선순위 TC 전체 PASS
- [ ] Medium 우선순위 TC 90% 이상 PASS
- [ ] TC-SEC-001, TC-SEC-002 필수 PASS (보안 요구사항)
- [ ] TC-CI-003 E2E 파이프라인 PASS
- [ ] 미해결 High 심각도 결함 없음

---

*본 문서는 ASPICE SWE.4/5/6 산출물로 관리되며, 테스트 수행 후 결과를 본 문서에 기록한다.*
