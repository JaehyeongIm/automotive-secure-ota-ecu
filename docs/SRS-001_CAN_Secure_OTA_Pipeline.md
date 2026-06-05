# SRS-001: CAN 기반 UDS over ISO-TP Secure OTA 파이프라인 요구사항 명세서

| 항목 | 내용 |
|---|---|
| 문서 ID | SRS-001 |
| 문서명 | CAN 기반 UDS over ISO-TP Secure OTA 파이프라인 요구사항 명세서 |
| 프로젝트명 | Dual ECU 버튼 트리거 직진 주행 Secure OTA 시스템 |
| 버전 | 2.11 |
| 작성일 | 2026-05-25 |
| 작성 목적 | Dual ECU 버튼 트리거 직진 주행 + 장애물 회피 OTA 데모 시스템 소프트웨어 요구사항 정의 |
| 주요 대상 | Raspberry Pi 5 Gateway, STM32F446RE ECU 2대, CAN Bus, Custom Bootloader, RC 차량 데모 플랫폼 |
| 범위 변경 | 1.7 대비 주행 방식 변경(라인트레이싱 → 버튼 트리거 직진), App v1/v2 2단계 OTA 구조 확정, Uptane 지연 활성화 도입, 장애물 임계값 단일화(10cm) |

---

## 1. 개요

### 1.1 프로젝트 목적

본 프로젝트의 목적은 Raspberry Pi 5를 차량 OTA Gateway 역할로, STM32F446RE 2대를 차량 ECU 역할로 구성하여 CAN 기반 Secure OTA 펌웨어 업데이트 파이프라인을 구현하는 것이다.

단순히 펌웨어 파일을 전송하는 데 그치지 않고, 다음 요소를 포함한 전장형 OTA 구조를 구현한다.

- STM32F446RE Custom Bootloader
- CAN 기반 UDS over ISO-TP 리프로그래밍 명령 구조
- 논리적 A/B App Slot 기반 안전 업데이트 및 Rollback
- Manifest 기반 펌웨어 무결성/인증성 검증
- SHA-256 Hash 검증
- 전자서명 기반 펌웨어 검증
- Anti-rollback 정책
- ECU ID / HW ID 기반 대상 검증
- 공격자 관점의 보안 시나리오 검증
- 버튼 트리거 직진 주행 + 장애물 회피 차량 플랫폼을 통한 OTA 적용 결과 실증

본 시스템은 양산 차량 ECU 환경을 완전히 재현하지 않으며, 가용 개발 플랫폼(STM32F446RE, Raspberry Pi 5) 범위 내에서 전장 OTA의 핵심 기능을 구현하고 검증하는 것을 목표로 한다. 구현 범위의 한계는 Section 19에 명시한다.

본 프로젝트는 요구사항 기반 개발과 양방향 추적성 원칙을 적용하여 SRS, 설계, 구현, 테스트, 트러블슈팅 산출물 간의 일관성을 유지한다.

### 1.2 시스템 목표

본 시스템이 달성해야 하는 핵심 목표는 다음과 같다.

| 목표 영역 | 구현 내용 |
|---|---|
| Custom Bootloader | MCU 메모리맵 기반 Flash 파티션 관리, App jump, Vector Table 재배치 |
| CAN 통신 | 차량 네트워크 기반 ECU 간 업데이트 명령 및 상태 메시지 송수신 |
| UDS-style 명령 구조 | 진단 기반 리프로그래밍 절차(Session Control, Security Access, Download, Transfer) 구현 |
| A/B Slot 및 Rollback | 비활성 Slot 대상 업데이트, Self-test 기반 Confirmed 처리, 실패 시 자동 복구 |
| Secure OTA | SHA-256 무결성 검증, ECDSA 서명 검증, Anti-rollback, ECU ID 검증 |
| 위협 시나리오 검증 | 변조, 위조, 다운그레이드, Replay, 미인증 명령, CAN Flood 공격 방어 검증 |
| 시스템 실증 | 버튼 트리거 직진 주행 차량 플랫폼을 통한 App v1/v2 OTA 적용 결과 기능 검증 |
| 요구사항 추적성 | SRS 요구사항에서 설계, 구현, 테스트, 트러블슈팅까지 양방향 추적 관리 |
| 계측 기반 검증 | 오실로스코프, 로직분석기, ST-LINK를 활용한 신호 수준 검증 및 원인 분석 |

---

## 2. 프로젝트 범위

### 2.1 In Scope

본 프로젝트에서 구현 대상에 포함되는 범위는 다음과 같다.

1. Raspberry Pi 5 기반 OTA Gateway 구현
2. STM32F446RE 기반 Drive ECU 구현
3. STM32F446RE 기반 Sensor/Body ECU 구현
4. 각 ECU의 Custom Secure Bootloader 구현
5. ISO-TP (SF/FF/CF/FC) 기반 UDS 전송 계층 구현
6. CAN 기반 UDS 업데이트 명령 구조 구현 (0x10, 0x27, 0x34, 0x36, 0x37, 0x31, 0x11)
7. 논리적 A/B App Slot 기반 업데이트 및 Rollback 구현
8. Manifest 기반 업데이트 패키지 검증
9. SHA-256 기반 이미지 무결성 검증
10. 전자서명 기반 펌웨어 인증성 검증
11. Anti-rollback 정책 구현
12. ECU ID / HW ID 기반 대상 검증
13. 버튼 트리거 직진 주행 App v1/v2 업데이트 실증 (장애물 감지 반응 단계별 차이 실증)
14. 공격자 시나리오 기반 보안 테스트
15. 정상/비정상 업데이트 로그 수집 및 테스트 리포트 작성
16. ASPICE-inspired 요구사항 기반 개발 프로세스 적용
17. 요구사항-설계-구현-테스트 간 추적성 매트릭스 작성
18. 계측기 기반 트러블슈팅 사례 수집 및 리포트 작성
19. ST-LINK 디버거 기반 Bootloader/App 실행 흐름 검증
20. Jenkins 기반 CI/CD 파이프라인 구현 (크로스컴파일, 정적 분석, 바이너리 크기 검사, 서명, OTA 자동 배포)

### 2.2 Out of Scope

다음 항목은 본 프로젝트의 1차 구현 범위에서 제외한다.

| 제외 항목 | 제외 사유 |
|---|---|
| Delta Update / 차분 업데이트 | 1차 구현에서는 안정성 검증을 우선하며, Full Image 기반 A/B OTA에 집중하기 위함 |
| 상용 Uptane 전체 구현 | 개인 프로젝트 범위에서 과도하므로 Uptane-lite 개념 참고 수준으로 제한 |
| AUTOSAR Classic/Adaptive 정식 스택 | 상용 스택 없이 개념 연결 수준으로 제한 |
| ISO-TP 멀티 세션 동시 처리 | 단일 ECU당 단일 OTA 세션만 운용하므로 동시 세션 관리는 제외 |
| HSM 기반 키 저장 | F446RE의 하드웨어 한계로 양산 ECU 수준 구현 불가 |
| 실제 차량 ECU 연결 공격 | 안전/법적 문제로 자체 제작 테스트 네트워크 내부에서만 검증 |
| 실제 차량 주행 제어 | 버튼 트리거 직진 주행 RC 차량 플랫폼에서만 실증 |
| ASPICE 완전 준수/공식 평가 | 개인 프로젝트 범위이므로 공식 ASPICE 심사나 조직 프로세스 수준의 준수는 제외 |
| 상용 요구사항 관리 도구 필수 사용 | Polarion, DOORS 등 상용 도구 대신 Markdown/Spreadsheet 기반 추적성 관리로 대체 |
| UN R156 SUMS 인증 | UNECE WP.29 UN R156 기반 Software Update Management System 공식 인증은 본 프로젝트 범위 제외. 개인 프로젝트 규모에서 인증 기관 심사는 불가 |

---

## 3. 시스템 개요

### 3.1 전체 시스템 구성

```text
[개발자 PC]
        |
        | Git Push (로컬 네트워크)
        v
[Raspberry Pi 5: Jenkins CI/CD 서버 + OTA Gateway]
  ┌─ CI/CD 파이프라인 (Jenkins) ──────────────────┐
  │  Stage 1: 크로스컴파일 (arm-none-eabi-gcc)    │
  │  Stage 2: 정적 분석 (cppcheck)                │
  │  Stage 3: 바이너리 크기 검사 (≤ 128KB)        │
  │  Stage 4: ECDSA 서명 + Manifest 생성          │
  │  Stage 5: OTA 배포 스크립트 실행              │
  └───────────────────────────────────────────────┘
  ┌─ OTA Gateway ─────────────────────────────────┐
  │  - ECU Inventory 수집                          │
  │  - Update Campaign 상태 관리                   │
  │  - 공격자 시나리오 실행                        │
  └───────────────────────────────────────────────┘
        |
        | CAN Bus
        v
+----------------------------+      +------------------------------+
| STM32F446RE ECU #1         |      | STM32F446RE ECU #2           |
| Drive ECU                  |      | Sensor/Body ECU              |
| - Custom Bootloader        |      | - Custom Bootloader          |
| - A/B App Slot             |      | - A/B App Slot               |
| - Button-Trigger Drive     |      | - Distance/Obstacle Sensing  |
| - Motor Control (PWM)      |      | - Status Message Tx          |
+----------------------------+      +------------------------------+
```

### 3.2 주요 하드웨어 역할

| 구성 요소 | 역할 |
|---|---|
| Raspberry Pi 5 | Jenkins CI/CD 서버, OTA Gateway, Update Manager, CAN 송수신, 로그 수집, 공격자 시나리오 실행 |
| STM32F446RE #1 | Drive ECU: 버튼 트리거 직진 주행, 모터 제어(PWM), 장애물 반응 로직, OTA 대상 ECU |
| STM32F446RE #2 | Sensor/Body ECU: HC-SR04 거리센서, 장애물 감지(10cm 임계값), 상태 메시지 송신, OTA 대상 ECU |
| CAN Transceiver | STM32와 CAN Bus 간 물리 계층 연결 |
| CANable 또는 유사 장치 | PC/Raspberry Pi에서 CAN 로그 확인 및 테스트 프레임 송신 |
| 2WD 차체 | OTA 적용 결과를 실증할 RC 차량 데모 플랫폼 |
| TB6612FNG | DC 모터 구동 드라이버 |
| B1 버튼 (USER button) | Drive ECU의 주행 트리거 입력 (EXTI PC13) |
| HC-SR04 초음파 센서 | Sensor/Body ECU의 장애물 감지 입력 |

---

## 4. 사용자 및 이해관계자

| 이해관계자 | 관심사 |
|---|---|
| 임베디드 소프트웨어 개발자 | Bootloader, CAN 드라이버, OTA 절차, 보안 검증 구현 |
| 시스템 통합 담당자 | ECU 간 인터페이스, CAN 메시지 명세, 업데이트 절차 일관성 |
| 품질 보증(QA) 담당자 | 요구사항 추적성, 테스트 커버리지, 실패 복구 검증 결과 |
| 차량 운전자 | 업데이트 실패 시 기존 주행 기능 유지, 업데이트 중 안전 정지 |

---

## 5. 운영 시나리오

### 5.1 정상 업데이트 시나리오

1. Raspberry Pi 5가 ECU Inventory를 요청한다.
2. 각 ECU는 현재 펌웨어 버전, 활성 슬롯, Boot 상태, ECU ID를 응답한다.
3. Raspberry Pi 5가 업데이트 Manifest를 확인한다.
4. Raspberry Pi 5가 대상 ECU에 UDS-style Diagnostic Session 진입을 요청한다.
5. ECU는 업데이트 가능한 조건인지 확인한다.
6. Raspberry Pi 5가 Security Access 절차를 수행한다.
7. Raspberry Pi 5가 Request Download를 전송한다.
8. ECU는 비활성 Slot을 선택하고 다운로드 준비 상태로 진입한다.
9. Raspberry Pi 5가 Transfer Data로 펌웨어 조각을 전송한다.
10. ECU는 조각 순서, 길이, 범위를 확인하고 비활성 Slot에 기록한다.
11. Request Transfer Exit 이후 ECU는 이미지 Hash, Signature, Version, ECU ID를 검증한다.
12. 검증 성공 시 ECU는 Boot Metadata를 갱신하여 다음 부팅 대상 Slot을 변경한다.
13. ECU Reset 이후 새 App으로 부팅한다.
14. App은 Self-test를 수행하고 정상일 경우 Confirmed 상태를 Bootloader에 알린다.
15. Confirmed 처리 완료 후 새 Slot이 정상 슬롯이 된다.

### 5.2 실패 복구 시나리오

1. 업데이트 중 전원 차단 또는 통신 중단이 발생한다.
2. 재부팅 시 Bootloader가 Metadata를 확인한다.
3. 새 Slot이 검증 완료/Confirmed 상태가 아니면 기존 Confirmed Slot으로 부팅한다.
4. 기존 App이 정상 동작해야 한다.
5. Raspberry Pi 5는 실패 원인을 로그로 기록한다.

### 5.3 공격자 검증 시나리오

1. 공격자 노드가 위조된 Update Start 명령을 전송한다.
2. ECU는 인증되지 않은 세션을 거부한다.
3. 공격자 노드가 변조된 Firmware Chunk를 전송한다.
4. ECU는 최종 Hash 또는 Signature 검증에서 실패 처리한다.
5. 공격자 노드가 이전 버전 펌웨어를 전송한다.
6. ECU는 Anti-rollback 정책에 의해 거부한다.
7. 공격자 노드가 Replay 프레임을 전송한다.
8. ECU는 Session ID, Sequence Number, Freshness Counter를 통해 거부한다.

---

## 6. 시스템 제약사항

### 6.1 MCU 제약

- STM32F446RE는 양산 차량용 HSM MCU가 아니다.
- F446RE의 Flash는 하드웨어 Dual Bank 구조가 아니므로, 단일 Flash 내부를 논리적으로 분할하여 A/B Slot을 구성한다.
- F446RE의 Flash 용량은 제한적이므로 App 크기, Bootloader 크기, 암호 라이브러리 크기를 관리해야 한다.
- 실제 양산 ECU 수준의 Secure Boot ROM, HSM Key Storage, Debug Lock 정책은 본 프로젝트에서 완전 재현하지 않는다.

### 6.2 CAN / ISO-TP 제약

- CAN Classic (최대 8바이트 payload) 기반으로 구현한다.
- CAN FD 수준의 대용량 Payload를 전제하지 않는다.
- 대용량 데이터(펌웨어 이미지) 전송은 ISO-TP FF/CF 분할 전송으로 처리한다.
- ISO-TP는 SF/FF/CF/FC 핵심 프레임 타입을 구현하며, 멀티 세션 동시 처리는 제외한다.
- Gateway(Raspberry Pi 5)는 Linux SocketCAN의 ISO-TP 커널 지원을 활용한다.
- ECU(STM32F446RE)는 ISO-TP 수신 상태 머신을 소프트웨어로 구현한다.
- CAN 버스 속도는 500Kbps로 운용한다.
- CAN Bus 부하, 전송 시간, STmin 타이밍, Timeout 정책을 고려한다.
- ISO-TP 운용 파라미터는 다음 값을 기준으로 한다.

| ISO-TP 파라미터 | 값 | 설명 |
|---|---|---|
| STmin | 0x00 (0ms) | CF 프레임 간 최소 간격 없음. Gateway가 최대 속도로 전송 |
| BlockSize | 0x00 | FC 없이 모든 CF를 연속 수신. ECU 처리 속도가 충분한 경우 적용 |
| N_Bs Timeout | 1000ms | FF 송신 후 FC 수신 대기 최대 시간 |
| N_Cr Timeout | 1000ms | CF 수신 대기 최대 시간. 초과 시 세션 abort |
| Transfer Data Chunk | 256 bytes | Transfer Data 1회당 전송 데이터 크기. 128KB 이미지 기준 512회 전송 |

### 6.3 프로젝트 범위 제약

- 본 프로젝트는 개인 사이드 프로젝트이며, 상용 OTA 시스템을 완전 대체하지 않는다.
- Uptane, AUTOSAR, UDS는 전체 표준 구현이 아니라 개념을 참고한 축소 구현으로 제한한다.
- ISO-TP는 OTA에 필요한 SF/FF/CF/FC 핵심 기능을 구현하며, ISO 15765-2 전체 규격 준수를 목표로 하지 않는다.
- 보안 공격 시나리오는 자체 제작한 테스트 환경 내부에서만 수행한다.

---

## 7. 기능 요구사항

### 7.1 OTA Gateway 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| FR-GW-001 | Raspberry Pi 5는 OTA Gateway 역할을 수행해야 한다. | Must | Gateway 프로세스가 ECU 목록, 버전, 상태를 수집할 수 있어야 한다. |
| FR-GW-002 | Gateway는 Update Package와 Manifest를 읽고 파싱해야 한다. | Must | Manifest 필드를 정상적으로 파싱하고 누락 필드를 오류 처리한다. |
| FR-GW-003 | Gateway는 Manifest에 포함된 ECU ID, Version, Image Size, Hash, Signature 정보를 관리해야 한다. | Must | Manifest 내용이 로그에 기록되어야 한다. |
| FR-GW-004 | Gateway는 대상 ECU별 업데이트 순서를 제어해야 한다. | Must | Drive ECU, Sensor ECU를 개별적으로 업데이트할 수 있어야 한다. |
| FR-GW-005 | Gateway는 CAN을 통해 UDS-style 명령을 송수신해야 한다. | Must | Session Control, Request Download, Transfer Data 등 핵심 명령이 동작해야 한다. |
| FR-GW-006 | Gateway는 업데이트 성공/실패 결과를 로그로 저장해야 한다. | Must | 각 ECU별 업데이트 결과, 실패 원인, 시간 정보가 기록되어야 한다. |
| FR-GW-007 | Gateway는 공격자 시나리오 실행 모드를 제공해야 한다. | Should | Replay, 변조, 비인가 명령, Flood 테스트를 선택 실행할 수 있어야 한다. |
| FR-GW-008 | Gateway는 Update Campaign ID를 관리해야 한다. | Should | 동일 Campaign에 속한 ECU 업데이트 결과를 묶어서 기록해야 한다. |

### 7.2 ECU Bootloader 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| FR-BL-001 | 각 STM32F446RE ECU는 Custom Bootloader를 포함해야 한다. | Must | Reset 후 Bootloader가 먼저 실행되어야 한다. |
| FR-BL-002 | Bootloader는 현재 활성 Slot과 후보 Slot을 구분해야 한다. | Must | Metadata에서 active 슬롯과 각 슬롯의 5상태(§7.3.1: INVALID/UPDATING/UPDATED/TRIAL/CONFIRMED)를 읽을 수 있어야 한다. |
| FR-BL-003 | Bootloader는 유효한 App이 있을 경우 해당 App으로 jump해야 한다. | Must | Vector Table 설정 후 App Reset Handler로 정상 진입해야 한다. |
| FR-BL-004 | Bootloader는 CAN 기반 업데이트 세션에 진입할 수 있어야 한다. | Must | 특정 조건 또는 명령 수신 시 update mode에 진입해야 한다. |
| FR-BL-005 | Bootloader는 수신한 펌웨어 조각을 비활성 Slot에 기록해야 한다. | Must | Flash erase/write가 섹터 경계와 주소 범위 내에서 수행되어야 한다. |
| FR-BL-006 | Bootloader는 펌웨어 전체 Hash를 검증해야 한다. | Must | Manifest의 Hash와 실제 Slot 이미지 Hash가 일치해야 한다. |
| FR-BL-007 | Bootloader는 펌웨어 Signature를 검증해야 한다. | Must | 공개키 기반 검증 실패 시 업데이트를 거부해야 한다. |
| FR-BL-008 | Bootloader는 Firmware Version을 확인하고 다운그레이드를 차단해야 한다. | Must | 현재 Confirmed Version보다 낮은 버전은 거부해야 한다. 구현: **ADR-007**(앞 서명 헤더 fw_version vs 메타 CONFIRMED 슬롯 버전; 기준선 변조방지는 ATECC608A 후속). |
| FR-BL-009 | Bootloader는 Target ECU ID/HW ID를 확인해야 한다. | Must | 다른 ECU용 이미지 설치를 거부해야 한다. |
| FR-BL-010 | Bootloader는 업데이트 후 첫 부팅에서 Self-test 결과를 확인해야 한다. | Must | Self-test 실패 시 이전 Confirmed Slot으로 rollback해야 한다. |
| FR-BL-011 | Bootloader는 Boot Metadata 손상 시 안전한 기본 정책을 적용해야 한다. | Should | Metadata CRC 오류 시 기존 Confirmed Slot 또는 Safe Mode로 진입해야 한다. |
| FR-BL-012 | Bootloader는 IWDG(Independent Watchdog)를 사용하여 MCU 무응답 상태를 감지해야 한다. | Must | IWDG 타임아웃은 8000ms로 설정하며, Flash erase/write 구간에서 주기적으로 피딩하여 의도치 않은 리셋을 방지해야 한다. Transfer Data 미수신 5000ms 초과 시 소프트웨어적으로 세션을 abort하고 기존 App을 유지해야 한다. |
| FR-BL-013 | Bootloader Flash 영역은 STM32 WRP(Write Protection)로 보호되어야 한다. | Should | Bootloader 섹터(0x08000000~0x08007FFF, Sector 0~1)에 WRP가 설정된 상태에서 해당 영역 write 시도가 거부되어야 한다. Metadata(섹터 2·3)와 App Slot(섹터 4~7)은 OTA 중 기록 가능해야 하므로 WRP 범위에서 제외한다(§14.2). |

### 7.3 A/B Slot 및 Rollback 요구사항

#### 7.3.1 슬롯 상태 모델

본 절의 슬롯 생명주기는 **ISO 24089(Software update engineering)** 및 **AUTOSAR Adaptive UCM(Update and Configuration Management)**의 *준비 → 정적 검증 → 활성화 → 시험(trial) → 확정/롤백* 흐름을 따라 다음 5개 상태로 명시한다. 각 슬롯(A/B)은 독립적으로 상태를 가진다. (이전 버전의 PENDING은 UPDATED와 TRIAL로 분리하여, "기록만 됨"과 "시험 부팅됨"을 구분한다.)

| 상태 | 의미 | 진입 시점 |
|---|---|---|
| INVALID | 유효 이미지 없음 또는 불량으로 마킹됨 | 초기/erase 직후, 3-strike 실패 시 |
| UPDATING | 이미지 기록 진행 중(erase/write) | RequestDownload~TransferData 구간 |
| UPDATED | 기록 완료 + 정적 검증(헤더 서명·hash·version) 통과, 첫 부팅 대기 | TransferExit + 검증 통과 |
| TRIAL | 시험 부팅 선택됨(boot_attempt_count 증가), Self-test 진행 중 | 부트로더가 UPDATED 슬롯으로 점프 직전 |
| CONFIRMED | Self-test 통과, known-good | Self-test PASS 시 |

#### 7.3.2 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| FR-AB-001 | 시스템은 논리적 A/B App Slot을 사용하며, 각 슬롯은 INVALID/UPDATING/UPDATED/TRIAL/CONFIRMED 5상태(§7.3.1)로 관리해야 한다. | Must | 임의 시점에 CONFIRMED-active 슬롯은 최대 1개여야 한다. 두 슬롯이 모두 CONFIRMED이면 active_slot이 우선하고, 모두 비유효(INVALID/검증실패)이면 Safe State(NFR-SAFE-004)로 진입해야 한다. |
| FR-AB-002 | 업데이트 이미지는 실행 중이 아닌 비활성 Slot에만 기록하며, 기록 구간 동안 해당 슬롯을 UPDATING으로 표시해야 한다. | Must | 현재 CONFIRMED Slot은 업데이트 과정에서 erase/write되지 않아야 한다. |
| FR-AB-003 | 새 Slot은 정적 검증(이미지 헤더 서명·hash·version, FR-AB-008)을 통과하기 전까지 Boot Target으로 선택되지 않아야 한다. 검증 불가·헤더 부재는 PASS가 아니라 **검증 실패(fail-closed)**로 처리한다. | Must | 헤더/서명/hash/version 중 하나라도 검증 실패 또는 검증 불가 시 기존 CONFIRMED Slot이 유지되어야 한다. `image_size==0` 등 검증을 우회하는 경로가 존재하지 않아야 한다(ISO 24089/R155 deny-by-default). |
| FR-AB-004 | 새 App은 부팅 후 **측정 가능한 Self-test**를 통과해야 CONFIRMED로 전이되며, Confirm 주체는 ECU 자신이어야 한다. | Must | 다음 4개를 부팅 후 Tboot 이내 모두 만족 시 PASS: ① app main 루프 진입, ② CAN/Timer/모터·센서 페리페럴 init 성공, ③ CAN heartbeat 1회 송신 성공, ④ 런타임 APP_VERSION == 이미지 헤더 version(FR-AB-008). PASS 시 ECU가 메타를 TRIAL→CONFIRMED로 기록한다. Gateway의 완료 명령만으로는 CONFIRMED되지 않아야 한다(SR-ATK-009). **Tboot**는 타깃 실측 worst-case(리셋~Self-test 완료)의 2배 이상 여유로 유도하되 IWDG 윈도우(8000ms) 미만이어야 하며, 측정 방식은 **ADR-002**를 따른다. |
| FR-AB-005 | 업데이트(특히 메타데이터 갱신) 도중 전원 차단이 발생해도 기존 CONFIRMED App이 유지되어야 하며, 메타데이터는 **원자적으로** 갱신되어야 한다. | Must | 메타데이터를 **2개의 분리된 플래시 섹터에 이중화(redundant)**하고 각 사본에 CRC32와 monotonic `seq_counter`를 둔다. 갱신은 구(舊) 사본에 기록하고 CRC를 **마지막에** 기록하며, 부팅 시 CRC가 유효하고 seq가 최신인 사본을 선택한다(AUTOSAR NvM redundant-block 패턴). 메타 쓰기 중 전원 차단 테스트 후 기존 App으로 정상 부팅해야 한다. |
| FR-AB-006 | Boot Metadata는 슬롯 5상태(slot_a/b_status), active_slot, slot 버전, boot_attempt_count, slot 크기, metadata_crc, seq_counter를 관리해야 한다. | Should | §13.3의 필드를 모두 포함하고, metadata dump 또는 로그에서 상태 확인이 가능해야 한다. |
| FR-AB-007 | 부트로더는 UPDATED 슬롯으로 점프하기 **직전** boot_attempt_count를 비휘발성으로 1 증가시키고, Self-test PASS 시 0으로 리셋하며 CONFIRMED로 전이해야 한다. count가 3을 초과하면 해당 슬롯을 INVALID로 마킹하고 이전 CONFIRMED Slot으로 롤백해야 한다. | Should | hang·crash·reset 등 Self-test를 완료하지 못한 **모든** 경우가 카운트되어, 3회 초과 시 자동 롤백이 확인되어야 한다(증가-먼저-점프). 임계값 3과 롤백 총지연(3×부팅시간)은 ISO 26262 FTTI 범위 및 IWDG 윈도우 내여야 한다. |
| FR-AB-008 | 각 App 이미지는 고정 오프셋(offset 0)에 **서명으로 보호되는 Image Header**를 포함해야 한다. | Must | 헤더는 `{magic, header_version, target_ecu_id, hardware_id, fw_version, image_size, image_hash}`를 포함하고, ECDSA 서명이 (헤더+코드)를 함께 덮어야 한다. 부트로더는 이미지를 **실행하지 않고** 헤더만 읽어 anti-rollback(fw_version, FR-BL-008/SR-FW-003)과 ECU ID 일치(FR-BL-009)를 검증한다. 기대 fw_version의 출처는 서명된 Manifest(§13.1)이며, ECU는 Gateway의 Manifest를 신뢰하지 않고 헤더+서명으로 독립 검증해야 한다(SR-UP-004, Uptane). |

### 7.4 CAN / ISO-TP / UDS 프로토콜 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| FR-CAN-001 | 시스템은 CAN 기반 ECU 업데이트 통신을 수행해야 한다. | Must | Gateway와 각 ECU 간 CAN 송수신이 가능해야 한다. |
| FR-CAN-002 | 모든 UDS 메시지는 ISO-TP 전송 계층을 통해 송수신되어야 한다. | Must | ISO-TP 없이 직접 CAN 프레임으로 UDS 명령을 송수신하지 않아야 한다. |
| FR-CAN-003 | ECU는 ISO-TP SF(Single Frame)를 수신하여 UDS 명령을 처리해야 한다. | Must | 7바이트 이하 UDS 명령이 SF로 수신되어 정상 처리되어야 한다. |
| FR-CAN-004 | ECU는 ISO-TP FF/CF(First Frame / Consecutive Frame)를 수신하여 UDS 페이로드를 조립해야 한다. | Must | 다중 프레임으로 분할된 Transfer Data 페이로드가 누락 없이 조립되어야 한다. |
| FR-CAN-005 | ECU는 FF 수신 후 ISO-TP FC(Flow Control) 프레임을 송신해야 한다. | Must | FC 프레임의 FlowStatus, BlockSize, STmin 필드가 정확히 설정되어야 한다. |
| FR-CAN-006 | ECU는 ISO-TP STmin 타이밍을 준수해야 한다. | Must | CF 프레임 간 수신 간격이 STmin 이하일 경우 오류 처리해야 한다. |
| FR-CAN-007 | Gateway는 SocketCAN ISO-TP 인터페이스를 통해 UDS 메시지를 송수신해야 한다. | Must | Python socket(AF_CAN, SOCK_DGRAM, CAN_ISOTP) 기반으로 전송되어야 한다. |
| FR-CAN-008 | 명령 구조는 UDS 리프로그래밍 절차를 따르며, FR-CAN-009~015가 이를 구성하는 개별 요구사항이다. | Must | FR-CAN-009~015 요구사항이 모두 구현되어야 한다. |
| FR-CAN-009 | Diagnostic Session Control 명령을 지원해야 한다. | Must | Default(0x01)와 **Programming Session(0x02, ISO 14229)** 상태를 구분해야 한다. (현 코드의 "Extended" 명칭은 Programming으로 정정 대상) |
| FR-CAN-010 | Security Access 명령을 지원해야 한다. | Must | ECU가 4바이트 Seed(소프트웨어 nonce — TRNG 부재, ADR-004)를 발급하고, Gateway가 **HMAC-SHA256(key=PSK, msg=Seed)**의 앞 4바이트를 Key로 응답하면 인증이 성공해야 한다. 연속 3회 Key 오류 시 NRC 0x36(exceededNumberOfAttempts) 반환 + 10초 잠금(잠금 중 NRC 0x37). 상세 §13.6. |
| FR-CAN-011 | Request Download 명령을 지원해야 한다. | Must | image_size·target_slot·**target_ecu_id·hardware_id**를 확인하고 불일치 시 즉시 NRC 0x31/0x33으로 거부(fail-fast)해야 한다. firmware_version이 현 CONFIRMED보다 낮으면 거부(SR-FW-003); 최종 anti-rollback은 부팅 시 서명 헤더(FR-BL-008). **구현 현황:** `target_ecu_id`는 위조불가 서명 헤더 기반으로 RequestTransferExit(0x37)에서 강제(ADR-009); `hardware_id`는 단일 보드종(F446) 구성에서 잉여이므로 후속으로 이연(§19.1 L-2). |
| FR-CAN-012 | Transfer Data 명령을 지원해야 한다. | Must | Sequence Number와 Chunk 길이를 확인하고, **누적 수신이 RequestDownload의 image_size를 초과하면 NRC 0x31(requestOutOfRange)로 거부 후 세션 종료**해야 한다(endless-data 방어, SR-ATK-007). |
| FR-CAN-013 | Request Transfer Exit 명령을 지원해야 한다. | Must | **누적 수신 == image_size를 확인**(불일치 시 NRC 0x24/0x72)한 뒤 검증 단계로 진입해야 한다. |
| FR-CAN-014 | Routine Control - Verify Image 명령을 지원해야 한다. | Must | Hash/Signature/Version/ECU ID 검증 결과를 반환하며, **검증을 통과해야만 슬롯을 UPDATED로 표시**(미통과·미수행 시 부팅 대상 불가, fail-closed, FR-AB-003 연계)해야 한다. |
| FR-CAN-015 | ECU Reset 명령을 지원해야 한다. | Must | 검증 성공 후 Reset을 통해 새 Slot 부팅을 시도해야 한다. |
| FR-CAN-016 | Negative Response Code를 정의해야 한다. | Must | 잘못된 세션, 보안 실패, 길이 오류, 순서 오류, 인증 실패 등을 구분해야 한다. |
| FR-CAN-017 | Replay 방어를 위해 세션 nonce(SecurityAccess Seed 파생 session_id)를 사용해야 한다. | Should | 각 프로그래밍 세션은 session_id를 가지며 TransferData가 이를 참조하여, 이전 세션 메시지의 재전송이 거부되어야 한다(SR-ATK-005). Seed freshness 한계는 ADR-004(개선 ADR-006). |
| FR-CAN-018 | Read Data By Identifier(0x22) 명령을 지원해야 한다. | Should | DID로 APP_VERSION·active_slot·target_ecu_id·boot 상태를 조회할 수 있어야 한다(ECU Inventory, SR-UP-001 / Uptane version-report). |
| FR-CAN-019 | UDS 세션은 S3 타임아웃을 적용해야 한다. | Must | 마지막 요청 후 5000ms(ISO 14229 S3server) 동안 무요청이면 Default 세션으로 복귀(세션 abort)하고 기존 App을 유지해야 한다(SR-ATK-008, FR-BL-012·NFR-REL-003 연계). |

### 7.5 Drive ECU Application 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| FR-DRV-001 | Drive ECU는 TB6612FNG를 통해 좌우 모터를 PWM 제어해야 한다. | Must | PWM 기반 좌우 모터 속도 제어 및 후진 방향 제어가 가능해야 한다. |
| FR-DRV-002 | Drive ECU는 B1(USER) 버튼 입력을 EXTI 인터럽트로 감지하여 주행을 시작해야 한다. | Must | B1 버튼 누름 후 300ms 디바운스 처리 후 g_button_pressed 플래그가 설정되어 DRIVE_IDLE → DRIVE_RUNNING 전이가 발생해야 한다. |
| FR-DRV-003 | Drive ECU App v1은 버튼 트리거 직진 주행 중 장애물이 10cm 이내 감지되면 즉시 정지해야 한다. | Must | 주행 시작 후 최대 3000ms 직진하며, 10cm 이하 장애물 감지 시(g_obstacle_flag) 즉시 모터를 정지하고 DRIVE_IDLE로 복귀해야 한다. |
| FR-DRV-004 | Drive ECU App v2는 v1 정지 후 자동 후진 복귀를 구현해야 한다. | Should | 10cm 이하 정지 후 300ms 대기, 600ms 후진(SLOW_SPEED), DRIVE_IDLE 복귀 순서로 동작해야 한다. |
| FR-DRV-006 | Drive ECU는 현재 App Version과 Slot 정보를 CAN으로 보고해야 한다. | Must | CAN ID 0x100 heartbeat에 APP_VERSION, 활성 슬롯, 주행 상태, 장애물 상태가 포함되어야 한다. |
| FR-DRV-007 | Drive ECU는 OTA 다운로드(TransferData) 진행 중에도 주행 기능을 유지해야 한다. | Must | RequestDownload(0x34) 수신 후 g_ota_active=1 구간(Flash Erase ~4초)에만 모터가 정지되며, 그 외 TransferData 구간에서는 drive_update()가 정상 실행되어야 한다. |
| FR-DRV-008 | Drive ECU는 DRIVE_IDLE 상태 진입 시 g_fw_pending 플래그를 확인하여 재부팅으로 펌웨어를 활성화해야 한다. | Must | TransferExit(0x37) 완료 후 g_fw_pending=1이 설정되고, 다음 DRIVE_IDLE 진입 시 NVIC_SystemReset()이 호출되어 Bootloader가 새 슬롯으로 부팅해야 한다. (Uptane 지연 활성화) |

### 7.6 Sensor/Body ECU Application 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| FR-SEN-001 | Sensor ECU는 HC-SR04 초음파 거리센서 입력을 읽어야 한다. | Must | Trigger/Echo GPIO 방식으로 거리값을 주기적으로 측정할 수 있어야 한다. |
| FR-SEN-002 | Sensor ECU는 장애물 감지 임계값 10cm를 사용해야 한다. | Must | 측정 거리가 10cm 이하일 때 obstacle_detected=1 및 실제 거리값(cm)을 CAN ID 0x200으로 송신해야 한다. Drive ECU는 이 값으로 g_obstacle_flag 및 g_distance_cm을 갱신한다. |
| FR-SEN-003 | Sensor ECU는 100ms 주기로 Alive/Heartbeat 메시지를 CAN으로 송신해야 한다. | Should | Drive ECU 또는 Gateway가 300ms 이내 Heartbeat를 수신하지 못하면 Sensor ECU를 dead로 간주할 수 있어야 한다. |
| FR-SEN-004 | Sensor ECU는 측정한 거리값(cm)을 CAN ID 0x200 페이로드 byte[1:2]에 포함하여 송신해야 한다. | Must | Drive ECU가 수신한 g_distance_cm 값이 Sensor ECU 실측값과 일치해야 한다. |
| FR-SEN-005 | Sensor ECU는 현재 App Version과 Slot 정보를 CAN으로 보고해야 한다. | Must | CAN ID 0x201 heartbeat에 APP_VERSION, 활성 슬롯 정보가 포함되어야 한다. |

### 7.7 CI/CD 파이프라인 요구사항

Jenkins 기반 CI/CD 파이프라인은 Raspberry Pi 5에서 운용되며, Git push 이벤트를 트리거로 펌웨어 빌드부터 ECU OTA 배포까지 자동 수행한다.

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| FR-CICD-001 | Jenkins 서버는 Raspberry Pi 5에 설치되어 운용되어야 한다. | Must | Jenkins 웹 UI에 접근 가능하고 파이프라인 실행 이력이 기록되어야 한다. |
| FR-CICD-002 | Git 저장소에 push 이벤트 발생 시 Jenkins 파이프라인이 자동으로 트리거되어야 한다. | Must | 소스코드 커밋 후 수동 개입 없이 파이프라인이 시작되어야 한다. |
| FR-CICD-003 | Stage 1에서 arm-none-eabi-gcc를 통해 Drive ECU 및 Sensor ECU 펌웨어를 크로스컴파일해야 한다. | Must | 빌드 오류 발생 시 이후 Stage가 실행되지 않고 파이프라인이 중단되어야 한다. |
| FR-CICD-004 | Stage 2에서 cppcheck를 통해 펌웨어 소스코드 정적 분석을 수행해야 한다. | Should | error 등급 이상의 결함 검출 시 파이프라인이 중단되고 결함 목록이 로그에 기록되어야 한다. |
| FR-CICD-005 | Stage 3에서 빌드된 펌웨어 바이너리 크기가 App Slot 크기(128KB)를 초과하는지 검사해야 한다. | Must | 초과 시 파이프라인이 중단되고 실제 크기와 한계 크기가 로그에 기록되어야 한다. |
| FR-CICD-006 | Stage 4에서 ECDSA 개인키로 펌웨어에 서명하고 Manifest를 생성해야 한다. | Must | 서명된 펌웨어 바이너리와 Manifest 파일이 Jenkins 아티팩트로 저장되어야 한다. |
| FR-CICD-007 | Stage 5에서 OTA 배포 스크립트는 주행 중에도 다운로드를 시작할 수 있으나, 실제 펌웨어 활성화(재부팅)는 ECU가 IDLE 상태가 된 후 자동으로 수행된다. | Must | UDS over ISO-TP 절차(0x10→0x27→0x34→0x36→0x37)로 비활성 슬롯에 펌웨어 기록을 완료한다. TransferExit 후 ECU는 g_fw_pending=1을 세트하고 즉시 재부팅하지 않는다. DRIVE_IDLE 상태 진입 시 ECU가 자동 재부팅하며, Jenkins는 heartbeat(CAN 0x100) 슬롯 전환을 확인한다. (Uptane 지연 활성화, FR-DRV-008 연계) |
| FR-CICD-008 | 각 Stage의 실행 결과와 로그가 Jenkins 빌드 이력에 기록되어야 한다. | Must | 성공/실패 여부, 실패 원인, 각 Stage 소요 시간이 Jenkins UI에서 확인 가능해야 한다. |
| FR-CICD-009 | ECDSA 개인키는 Jenkins Credentials로 관리되어야 하며 Jenkinsfile에 평문 노출되지 않아야 한다. | Must | Jenkinsfile 소스코드에 키 값이 존재하지 않아야 한다. |
| FR-CICD-010 | Stage 5 OTA 배포 실패 시 Jenkins 빌드를 FAILURE로 표시하고 실패 원인을 로그에 기록해야 한다. | Must | ECU 플래시 실패, Self-test 실패, Rollback 발생 중 어느 경우에도 Jenkins 빌드 상태가 FAILURE로 기록되어야 한다. 재시도는 수동으로만 수행한다. |

---

## 8. 보안 요구사항

### 8.1 Manifest 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| SR-MF-001 | 업데이트 패키지는 Manifest를 포함해야 한다. | Must | Manifest 없이는 업데이트가 시작되지 않아야 한다. |
| SR-MF-002 | Manifest는 target_ecu_id를 포함해야 한다. | Must | ECU ID 불일치 시 업데이트가 거부되어야 한다. |
| SR-MF-003 | Manifest는 firmware_version을 포함해야 한다. | Must | Version 기반 anti-rollback 검증이 가능해야 한다. |
| SR-MF-004 | Manifest는 image_size를 포함해야 한다. | Must | 수신 데이터가 image_size를 초과하면 중단해야 한다. |
| SR-MF-005 | Manifest는 image_hash를 포함해야 한다. | Must | 최종 Slot 이미지 Hash와 비교해야 한다. |
| SR-MF-006 | Manifest는 signature를 포함해야 한다. | Must | 공개키 기반 서명 검증을 수행해야 한다. |
| SR-MF-007 | Manifest는 campaign_id를 포함해야 한다. | Should | 복수 ECU 업데이트 결과를 하나의 캠페인으로 추적해야 한다. |
| SR-MF-008 | Manifest는 expiration 또는 metadata_version을 포함해야 한다. | Should | 오래된 Manifest 재사용을 방지해야 한다. |

### 8.2 Firmware 검증 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| SR-FW-001 | ECU는 수신한 펌웨어 전체에 대해 Hash를 계산해야 한다. | Must | 계산 Hash와 Manifest Hash가 다르면 업데이트 실패 처리해야 한다. |
| SR-FW-002 | ECU는 펌웨어 또는 Manifest Signature를 검증해야 한다. | Must | 검증 실패 시 Boot Target을 변경하지 않아야 한다. |
| SR-FW-003 | ECU는 현재 Confirmed Version보다 낮은 Version을 거부해야 한다. | Must | Downgrade 테스트가 실패 처리되어야 한다. |
| SR-FW-004 | ECU는 대상 ECU ID가 일치하지 않는 이미지를 거부해야 한다. | Must | Drive ECU용 이미지를 Sensor ECU에 설치할 수 없어야 한다. |
| SR-FW-005 | ECU는 이미지 크기가 Slot 크기를 초과하면 업데이트를 거부해야 한다. | Must | Slot boundary 침범이 발생하지 않아야 한다. |
| SR-FW-006 | ECU는 Flash write 주소 범위를 검증해야 한다. | Must | Bootloader, Metadata 영역에 App 데이터가 기록되지 않아야 한다. |

### 8.3 공격 방어 요구사항

| ID | 공격 시나리오 | 방어 요구사항 | 수용 기준 |
|---|---|---|---|
| SR-ATK-001 | Firmware Tampering | Hash/Signature 검증 | 1바이트 변조 이미지가 거부되어야 한다. |
| SR-ATK-002 | Arbitrary Software | Signature 검증 | 개인이 임의 생성한 unsigned image가 거부되어야 한다. |
| SR-ATK-003 | Downgrade Attack | Anti-rollback | 낮은 version image가 거부되어야 한다. |
| SR-ATK-004 | ECU Mismatch | target_ecu_id 검증 | 다른 ECU용 image가 거부되어야 한다. |
| SR-ATK-005 | Replay Attack | session_id, sequence number, freshness counter | 이전 Transfer Data 재전송이 거부되어야 한다. |
| SR-ATK-006 | Unauthorized Update Start / Brute Force | Security Access, 시도 횟수 제한 | 인증되지 않은 update command가 거부되어야 하며, 연속 3회 Key 오류 시 10초간 Security Access가 잠금되어야 한다. |
| SR-ATK-007 | Endless Data Attack | image_size, chunk_count 제한 | Manifest size 초과 수신 시 세션이 종료되어야 한다. |
| SR-ATK-008 | CAN Flood / DoS | timeout, abort, rollback | 업데이트 중단 후 기존 App으로 복구되어야 한다. |
| SR-ATK-009 | Fake Complete | ECU 내부 검증 결과 기반 commit | Gateway의 fake complete 명령만으로 Confirmed 되지 않아야 한다. |
| SR-ATK-010 | Partial Bundle Installation | campaign result 추적 | 한 ECU만 업데이트 성공 시 Campaign 상태가 partial/fail로 기록되어야 한다. |

### 8.4 키 관리 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| SR-KEY-001 | ECDSA 공개키는 Bootloader Flash 영역(Sector 0~1)에 하드코딩되어야 한다. | Must | 공개키가 App Slot 또는 Metadata 영역에 저장되지 않고 Bootloader 이미지 내에 포함되어야 한다. |
| SR-KEY-002 | ECDSA 개인키는 개발 PC 또는 Jenkins Credentials에만 존재해야 하며 ECU에 저장되지 않아야 한다. | Must | ECU Flash 어느 영역에도 개인키가 존재하지 않아야 한다. |
| SR-KEY-003 | Security Access에 사용하는 PreSharedKey는 Bootloader Flash 영역에 저장되어야 한다. | Must | PreSharedKey가 App Slot에 포함되지 않아야 한다. WRP가 적용된 Bootloader 영역에 위치해야 한다. |
| SR-KEY-004 | 공개키 및 PreSharedKey의 갱신은 Bootloader 재플래싱을 통해서만 가능해야 한다. | Should | ST-LINK 또는 전용 플래싱 도구를 통한 Bootloader 재기록으로만 키를 변경할 수 있어야 한다. |

---

## 9. Uptane-lite 참고 요구사항

### 9.1 적용 범위

본 프로젝트는 Uptane 전체 표준을 구현하지 않는다. 대신 자동차 OTA 보안에서 중요한 메타데이터 기반 검증 개념을 참고하여 Uptane-lite 구조로 축소 적용한다.

### 9.2 개념 매핑

| Uptane 개념 | 본 프로젝트 대응 |
|---|---|
| Primary ECU | Raspberry Pi 5 OTA Gateway |
| Secondary ECU | STM32F446RE Drive ECU, Sensor ECU |
| Image Repository | 개발 PC 또는 Raspberry Pi 5의 펌웨어 저장소 |
| Director Repository | Raspberry Pi 5의 ECU별 업데이트 지시 로직 |
| Targets Metadata | manifest.json |
| ECU Inventory | ECU ID, version, active slot, boot state 보고 |
| Version Report | 각 ECU의 현재 펌웨어 버전 보고 |
| Rollback 방어 | firmware_version, metadata_version, campaign_id 검증 |
| Partial Bundle 방어 | ECU별 업데이트 결과 수집 및 Campaign 상태 관리 |

### 9.3 Uptane-lite 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| SR-UP-001 | Gateway는 ECU Inventory를 수집해야 한다. | Must | 각 ECU의 version, slot, state가 기록되어야 한다. |
| SR-UP-002 | Manifest는 ECU별 대상 이미지를 명확히 구분해야 한다. | Must | Drive ECU와 Sensor ECU 이미지가 구분되어야 한다. |
| SR-UP-003 | Gateway는 Campaign 단위 업데이트 결과를 관리해야 한다. | Should | 전체 성공, 일부 성공, 전체 실패 상태가 구분되어야 한다. |
| SR-UP-004 | ECU는 Gateway 검증 결과만 신뢰하지 않고 자체 검증을 수행해야 한다. | Must | Gateway가 변조되었다는 가정에서도 unsigned image가 설치되지 않아야 한다. |
| SR-UP-005 | Manifest 재사용 공격을 방어해야 한다. | Should | metadata_version 또는 expiration 기반으로 오래된 Manifest를 거부해야 한다. |

---

## 10. 비기능 요구사항

### 10.1 신뢰성 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| NFR-REL-001 | 업데이트 실패 시 기존 정상 App이 유지되어야 한다. | Must | 실패 테스트 후 기존 버튼 트리거 주행 기능이 동작해야 한다. |
| NFR-REL-002 | 전원 차단 후에도 Bootloader가 일관된 상태로 복구해야 한다. | Must | 재부팅 후 Metadata 기반으로 안전 Slot을 선택해야 한다. |
| NFR-REL-003 | CAN 통신 중단 시 업데이트 세션이 무한 대기하지 않아야 한다. | Must | Timeout 후 abort 상태로 전환되어야 한다. |
| NFR-REL-004 | App Self-test 실패 시 rollback해야 한다. | Must | 실패 App이 Confirmed 처리되지 않아야 한다. |

### 10.2 성능 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| NFR-PERF-001 | CAN 전송은 Chunk 단위로 안정적으로 수행되어야 한다. | Must | 전체 이미지 전송 중 sequence 오류 없이 완료되어야 한다. |
| NFR-PERF-002 | 업데이트 시간은 측정되어야 한다. | Should | 이미지 크기, chunk 수, 전송 시간, 평균 throughput이 로그에 남아야 한다. |
| NFR-PERF-003 | drive_update() 제어 루프는 10ms 이하의 주기로 실행되어야 한다. | Should | CAN 수신 처리, 상태 전이, PWM 출력까지 전체 제어 루프가 10ms 이내에 완료되어야 한다. |

### 10.3 유지보수성 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| NFR-MNT-001 | CAN / ISO-TP / UDS 메시지 명세는 문서로 관리해야 한다. | Must | SDD-001 또는 CAN-001에 CAN ID, ISO-TP 프레임 구조, UDS SID/NRC 명세가 포함되어야 한다. |
| NFR-MNT-002 | Bootloader 상태 전이는 다이어그램으로 문서화해야 한다. | Must | SDD-001에 Bootloader 상태 전이 다이어그램이 포함되어야 한다. |
| NFR-MNT-003 | 테스트 케이스와 결과는 추적 가능해야 한다. | Must | 요구사항 ID와 테스트 ID가 매핑되어야 한다. |
| NFR-MNT-004 | App v1/v2 변경점은 릴리즈 노트로 관리해야 한다. | Should | v1(즉시 정지), v2(자동 후진 복귀) 기능 차이가 문서화되어야 한다. |

### 10.4 안전성 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| NFR-SAFE-001 | OTA 다운로드(TransferData) 중 주행은 허용되나, RequestDownload(0x34) 처리 중 Flash Erase 구간(~4초)에는 모터가 정지해야 한다. | Must | g_ota_active=1 구간에서 drive_update()가 motor_stop()을 호출해야 한다. TransferData 구간에서는 drive_update()가 정상 실행되어 주행이 가능해야 한다. 펌웨어 활성화(재부팅)는 DRIVE_IDLE 상태에서만 발생해야 한다. |
| NFR-SAFE-002 | 장애물 감지 시 Drive ECU는 정지 또는 감속해야 한다. | Should | 지정 거리 이하에서 모터 출력이 제한되어야 한다. |
| NFR-SAFE-003 | Bootloader 상태에서는 모터가 구동되지 않아야 한다. | Must | Bootloader 실행 중 PWM 출력이 비활성화되어야 한다. |
| NFR-SAFE-004 | 시스템 Safe State는 "모터 정지 + Bootloader 대기 + 기존 Confirmed Slot 유지"로 정의한다. | Must | 업데이트 실패, 검증 오류, 통신 중단, Watchdog 리셋 등 비정상 상황 발생 시 Safe State로 진입하고 기존 Confirmed App이 유지되어야 한다. |

---


## 11. 개발 프로세스 및 요구사항 추적성 요구사항

### 11.1 적용 원칙

본 프로젝트는 Automotive SPICE의 요구사항 기반 개발, 산출물 간 일관성, 양방향 추적성, 변경 영향 추적 원칙을 적용한다. 공식 ASPICE 조직 프로세스 인증 및 외부 평가는 본 프로젝트 범위에 포함하지 않는다.

### 11.2 프로세스 요구사항

| 요구사항 ID | 요구사항 내용 | 우선순위 | 검증 방법 |
|---|---|---:|---|
| PRC-001 | 프로젝트 요구사항은 SRS-001에 고유 ID를 부여하여 관리해야 한다. | High | 문서 리뷰 |
| PRC-002 | 주요 기능 요구사항은 설계 문서 또는 설계 항목으로 추적 가능해야 한다. | High | RTM 확인 |
| PRC-003 | 주요 보안 요구사항은 공격 시나리오 및 테스트 케이스와 연결되어야 한다. | High | SEC-001, TP-001 확인 |
| PRC-004 | 주요 테스트 케이스는 관련 요구사항 ID를 포함해야 한다. | High | TP-001, TR-001 확인 |
| PRC-005 | 구현 중 요구사항이 변경되면 개정 이력 또는 변경 로그에 기록해야 한다. | Medium | 변경 로그 확인 |
| PRC-006 | 실패/결함 사례는 원인, 분석 도구, 조치, 재검증 결과를 포함하여 기록해야 한다. | High | TSR-001 확인 |
| PRC-007 | Release Note는 App v1/v2 차이, 업데이트 대상 ECU, 버전, 검증 결과를 포함해야 한다. | Medium | RN-001 확인 |

### 11.3 추적성 관리 요구사항

| 요구사항 ID | 요구사항 내용 | 우선순위 | 검증 방법 |
|---|---|---:|---|
| RTM-001 | 각 요구사항은 Requirement ID, 설계 항목, 구현 파일/함수, 테스트 케이스, 결과를 연결해야 한다. | High | RTM-001 리뷰 |
| RTM-002 | 보안 요구사항은 최소 1개 이상의 공격 또는 실패 테스트와 연결되어야 한다. | High | RTM-001 리뷰 |
| RTM-003 | Bootloader 관련 요구사항은 App jump, Flash write, Slot selection, Rollback 테스트와 연결되어야 한다. | High | RTM-001 리뷰 |
| RTM-004 | CAN/UDS-style 요구사항은 정상 응답과 Negative Response 테스트를 모두 포함해야 한다. | Medium | TP-001 확인 |
| RTM-005 | 요구사항 변경 시 영향받는 설계/테스트 항목을 식별해야 한다. | Medium | 변경 로그 확인 |

### 11.4 추적성 예시

| Requirement ID | Design 항목 | Implementation 예시 | Test Case | 결과 |
|---|---|---|---|---|
| SR-FW-002 | Signature Verification | boot_verify_signature() | TC-SEC-002 | PASS/FAIL 기록 |
| FR-AB-004 | Rollback State Machine | boot_select_slot() | TC-FAIL-004 | PASS/FAIL 기록 |
| FR-CAN-012 | TransferData Sequence Check | uds_transfer_data_handler() | TC-FAIL-002 | PASS/FAIL 기록 |
| SR-ATK-005 | Replay Defense | session_counter_check() | TC-SEC-005 | PASS/FAIL 기록 |
| PRC-006 | Troubleshooting Record | TSR-001 | TC-DBG-001~ | PASS/FAIL 기록 |

---

## 12. 계측 및 디버깅 요구사항

### 12.1 적용 원칙

계측기 사용 자체를 시스템 기능 요구사항으로 분류하지 않는다. 그러나 전장 임베디드 개발에서 문제를 관찰하고 원인을 측정 근거로 분석하는 과정은 검증의 핵심이다. 따라서 본 프로젝트는 계측 및 디버깅 도구 사용을 검증/트러블슈팅 산출물 요구사항으로 관리한다.

### 12.2 사용 도구 및 목적

| 도구 | 사용 목적 | 주요 확인 항목 |
|---|---|---|
| 오실로스코프 | 전기적 신호 및 전원 품질 확인 | CANH/CANL 파형, 전원 리플, 모터 구동 시 전압 강하 |
| 로직분석기 | 디지털 신호 및 타이밍 확인 | GPIO, PWM, UART, 센서 입력 타이밍 |
| 멀티미터 | 정적 전압/저항/연결 상태 확인 | 전원 레일, GND 연결, 종단저항, 단락 여부 |
| ST-LINK 디버거 | MCU 내부 실행 흐름 분석 | Breakpoint, register, stack pointer, VTOR, Flash error flag |
| CANable 또는 SocketCAN | CAN 프레임 송수신/로그 | UDS-style 요청/응답, 공격 프레임, timeout, NRC |

### 12.3 디버깅/트러블슈팅 요구사항

| 요구사항 ID | 요구사항 내용 | 우선순위 | 검증 방법 |
|---|---|---:|---|
| DBG-001 | CAN 통신 이상 발생 시 CANH/CANL 파형 또는 CAN 로그를 통해 원인을 분석해야 한다. | Medium | TSR-001 |
| DBG-002 | 업데이트 중 MCU 리셋 또는 오동작 발생 시 전원 레일과 모터 구동 영향을 확인해야 한다. | Medium | TSR-001 |
| DBG-003 | Bootloader에서 App jump 실패 시 MSP, Reset Handler 주소, VTOR 설정을 ST-LINK로 확인해야 한다. | High | TSR-001 |
| DBG-004 | Flash erase/write 실패 시 HAL Flash error flag, sector boundary, write alignment를 확인해야 한다. | High | TSR-001 |
| DBG-005 | 버튼 트리거 주행 이상 발생 시 EXTI 인터럽트 동작, PWM 출력, 모터 드라이버 입력 신호를 확인해야 한다. | Low | TSR-001 |
| DBG-006 | 보안 검증 실패 사례는 로그상 실패 원인과 실제 입력 조건이 일치하는지 확인해야 한다. | High | TR-001, TSR-001 |

### 12.4 트러블슈팅 리포트 형식 요구사항

TSR-001에는 각 문제에 대해 다음 항목을 포함한다.

| 항목 | 설명 |
|---|---|
| Issue ID | 예: ISS-CAN-001, ISS-BL-001 |
| 증상 | 관찰된 문제 현상 |
| 재현 조건 | 문제가 발생하는 입력, 상태, 절차 |
| 초기 가설 | 가능한 원인 후보 |
| 사용 도구 | 오실로스코프, 로직분석기, 멀티미터, ST-LINK 등 |
| 측정/분석 결과 | 파형, 로그, 레지스터, 전압, 오류 플래그 등 |
| 원인 | 최종 원인 |
| 조치 | 코드/회로/배선/설계 수정 내용 |
| 재검증 | 동일 테스트 재수행 결과 |
| 연결 요구사항 | 관련 SRS/테스트 ID |

### 12.5 예상 트러블슈팅 사례

| Issue ID | 예상 문제 | 사용 도구 | 확인 포인트 | 기대 산출물 |
|---|---|---|---|---|
| ISS-CAN-001 | CAN 통신 불안정 | 오실로스코프, 멀티미터 | 종단저항, CANH/CANL 차동 파형 | 파형 캡처, 원인 분석 |
| ISS-PWR-001 | 모터 구동 시 ECU 리셋 | 오실로스코프, 멀티미터 | 전원 강하, GND 공유 문제 | 전원 분리/필터링 조치 기록 |
| ISS-BL-001 | Bootloader App jump 실패 | ST-LINK | MSP, Reset Handler, VTOR | 디버깅 캡처, 수정 전후 로그 |
| ISS-FL-001 | Flash write 실패 | ST-LINK | sector boundary, HAL error flag | sector map 수정 기록 |
| ISS-CAN-002 | TransferData 누락/순서 오류 | CANable, SocketCAN | sequence number, timeout | CAN 로그 및 NRC 기록 |
| ISS-SEN-001 | HC-SR04 거리 측정 오동작 | 로직분석기 | Trigger/Echo 타이밍, 거리값 이상 | 측정 주기/임계값 조정 기록 |

---
## 13. 데이터 및 메시지 요구사항

### 13.1 Manifest 예시 구조

```json
{
  "manifest_version": 1,
  "campaign_id": "CAMP-2026-001",
  "target_ecu_id": "DRIVE_ECU",
  "hardware_id": "F446RE-DRIVE-V1",
  "firmware_version": 2,
  "min_allowed_version": 1,
  "image_size": 98304,
  "image_hash_alg": "SHA-256",
  "image_hash": "<hex string>",
  "signature_alg": "ECDSA-P256 or Ed25519",
  "signature": "<hex string>",
  "metadata_version": 5,
  "expires_at": "2026-12-31T23:59:59+09:00"
}
```

### 13.2 CAN ID 할당 테이블

본 프로젝트에서 사용하는 CAN ID는 11-bit Standard CAN ID 기반으로 다음과 같이 할당한다.

#### UDS 진단 메시지 (Physical Addressing)

| CAN ID | 방향 | 용도 | 비고 |
|---:|---|---|---|
| 0x7DF | Gateway → 전체 ECU | UDS Functional Request (ECU Inventory 조회 등) | Broadcast |
| 0x7E0 | Gateway → Drive ECU | UDS Physical Request (OTA 명령) | 1:1 |
| 0x7E8 | Drive ECU → Gateway | UDS Response | 0x7E0 + 8 |
| 0x7E1 | Gateway → Sensor ECU | UDS Physical Request (OTA 명령) | 1:1 |
| 0x7E9 | Sensor ECU → Gateway | UDS Response | 0x7E1 + 8 |

#### Application 메시지

| CAN ID | 방향 | 용도 | 주기/이벤트 | 페이로드 크기 |
|---:|---|---|---|---|
| 0x100 | Drive ECU → CAN Bus | 상태 보고 (App Version, Slot, 주행 상태) | 200ms 주기 | 8 bytes |
| 0x200 | Sensor ECU → CAN Bus | 장애물 감지 상태 (obstacle_detected) | 이벤트 발생 시 | 4 bytes |
| 0x201 | Sensor ECU → CAN Bus | Heartbeat (Alive 신호) | 100ms 주기 | 2 bytes |

### 13.3 Boot Metadata 요구 필드

| 필드 | 설명 |
|---|---|
| magic | Metadata 유효성 확인용 고정 값 |
| metadata_version | Metadata 구조 버전 |
| active_slot | 현재 부팅 대상 Slot (0=A, 1=B). 슬롯별 생명주기는 slot_a/b_status 5상태로 표현하며 별도 confirmed/pending 포인터를 두지 않는다 |
| slot_a_version | Slot A 펌웨어 버전 |
| slot_b_version | Slot B 펌웨어 버전 |
| slot_a_status | Slot A 상태 (INVALID/UPDATING/UPDATED/TRIAL/CONFIRMED, §7.3.1) |
| slot_b_status | Slot B 상태 (INVALID/UPDATING/UPDATED/TRIAL/CONFIRMED, §7.3.1) |
| boot_attempt_count | TRIAL 슬롯 부팅 시도 횟수(코드 필드: boot_count). 점프 직전 +1, self-test PASS 시 0 리셋, 3 초과 시 롤백(FR-AB-007) |
| slot_a_size / slot_b_size | 각 슬롯 서명 이미지 크기(부트로더 ECDSA 검증용) |
| last_error | 마지막 업데이트/부팅 실패 원인 (예약) |
| metadata_crc | Metadata 손상 검출용 CRC32 |
| seq_counter | 이중화 사본 중 최신본 식별용 monotonic 카운터 |

> Metadata는 §14.2의 **섹터 2·3에 이중화(redundant copy)**하여 저장한다. 부팅 시 `metadata_crc`(CRC32)가 유효하고 `seq_counter`가 최신인 사본을 선택하며, 갱신은 비활성 사본에 본문을 기록한 뒤 CRC를 **마지막에** 기록하여 원자적 commit을 보장한다(FR-AB-005, AUTOSAR NvM redundant-block 패턴).

### 13.4 UDS-style 명령 요구사항

| SID | 명령명 | 용도 |
|---:|---|---|
| 0x10 | Diagnostic Session Control | Programming Session 진입 |
| 0x27 | Security Access | 업데이트 권한 확인 |
| 0x34 | Request Download | 다운로드 조건 협상 |
| 0x36 | Transfer Data | 펌웨어 Chunk 전송 |
| 0x37 | Request Transfer Exit | 데이터 전송 종료 |
| 0x31 | Routine Control | 이미지 검증, Self-test, Confirm 처리 |
| 0x11 | ECU Reset | 업데이트 후 재부팅 |
| 0x22 | Read Data By Identifier | ECU ID, Version, Slot 상태 조회 |
| 0x7F | Negative Response | 오류 응답 |

### 13.5 주요 Negative Response Code 예시

| NRC | 의미 |
|---:|---|
| 0x10 | General Reject |
| 0x11 | Service Not Supported |
| 0x12 | Sub-function Not Supported |
| 0x22 | Conditions Not Correct |
| 0x24 | Request Sequence Error |
| 0x31 | Request Out Of Range |
| 0x33 | Security Access Denied |
| 0x35 | Invalid Key |
| 0x36 | Exceeded Number Of Attempts |
| 0x72 | General Programming Failure |
| 0x73 | Wrong Block Sequence Counter |

### 13.6 Security Access 파라미터

| 파라미터 | 값 | 설명 |
|---|---|---|
| Seed 크기 | 4 bytes | STM32F446는 하드웨어 TRNG가 없어, 96-bit UID + SysTick + 롤링 카운터를 SHA-256으로 혼합한 **소프트웨어 nonce**로 생성한다(NIST SP 800-90 DRBG 아님 — 엔트로피 약함·재부팅 replay 한계, **ADR-004**) |
| Key 계산 방식 | HMAC-SHA256(key=PSK, msg=Seed)[0:4] | PSK를 키, Seed를 메시지로 한 HMAC(RFC 2104)의 앞 4바이트를 Key로 사용한다 |
| PreSharedKey 크기 | 32 bytes | WRP 보호 Bootloader Flash 고정주소 `0x08007FE0`에 저장(SR-KEY-003), 앱이 read. Gateway는 env `OTA_PSK_HEX`로 동일 PSK 공유 |
| 최대 시도 횟수 | 3회 | 연속 3회 Key 오류 시 NRC 0x36(exceededNumberOfAttempts) 반환 |
| 잠금 지속 시간 | 10,000ms | 잠금 중 추가 SecurityAccess 요청은 NRC 0x37(requiredTimeDelayNotExpired). 현재 구현은 **RAM 기반**(전원 리셋 시 해제) — NV 지속(리셋 우회 차단)은 후속 과제(메타데이터 이중화 연계, **ADR-003**) |

---

## 14. Flash Partition 요구사항

### 14.1 기본 원칙

- F446RE의 하드웨어 Dual Bank Flash를 사용하는 것이 아니라, 단일 Flash를 논리적으로 분할한다.
- Bootloader 영역은 업데이트 중 덮어쓰지 않는다.
- 현재 Confirmed App Slot은 새 이미지 검증이 완료될 때까지 유지한다.
- Metadata는 App 영역과 분리하여 관리한다.

### 14.2 확정 파티션 레이아웃

STM32F446RE Linker Script 및 실제 구현 기준으로 확정된 파티션.

```text
주소          영역                크기    섹터
0x08000000  Bootloader Area     32KB    섹터 0 (16KB) + 섹터 1 (16KB)
0x08008000  Metadata Copy A     16KB    섹터 2
0x0800C000  Metadata Copy B     16KB    섹터 3
0x08010000  Slot A Application  192KB   섹터 4 (64KB) + 섹터 5 (128KB)
0x08040000  Slot B Application  256KB   섹터 6 (128KB) + 섹터 7 (128KB)
```

- Metadata: 섹터 2(`0x08008000`)·섹터 3(`0x0800C000`)에 **이중화(redundant) 저장**(FR-AB-005). 부팅 시 CRC 유효·seq 최신 사본을 선택하고, 갱신은 비활성 사본에 기록 후 CRC를 마지막에 기록한다. 두 사본 모두 Bootloader/WRP 영역(섹터 0~1) 밖이라 OTA 중 기록 가능하다.
- Slot A Linker Script: `FLASH ORIGIN=0x08010000, LENGTH=192K`
- Slot B Linker Script: `FLASH ORIGIN=0x08040000, LENGTH=256K`
- 두 슬롯의 크기 차이(192KB vs 256KB)는 섹터 구조상 불가피하며, 펌웨어 크기 검증 시 슬롯별 상한을 개별 적용한다.

### 14.3 파티션 관련 요구사항

| ID | 요구사항 | 우선순위 | 수용 기준 |
|---|---|---:|---|
| FR-FL-001 | Linker Script는 Bootloader와 App 주소를 분리해야 한다. | Must | App이 지정된 Slot 시작 주소에서 빌드되어야 한다. |
| FR-FL-002 | Bootloader는 App Vector Table 주소를 검증해야 한다. | Must | Stack Pointer와 Reset Handler가 유효 범위 내에 있어야 한다. |
| FR-FL-003 | Flash erase/write는 Slot 경계 내에서만 수행되어야 한다. | Must | Bootloader와 Metadata 영역 침범이 없어야 한다. |
| FR-FL-004 | App 크기가 Slot 크기를 초과하면 빌드 또는 업데이트 단계에서 실패 처리해야 한다. | Must | 초과 이미지가 설치되지 않아야 한다. |
| FR-FL-005 | Metadata는 손상 검출 정보를 포함해야 한다. | Should | metadata_crc 불일치 시 안전 정책으로 복구해야 한다. |

---

## 15. 테스트 요구사항

### 15.1 정상 기능 테스트 (SWE.6 — 시스템 수준)

| 테스트 ID | 관련 요구사항 | 테스트 내용 | 기대 결과 |
|---|---|---|---|
| TC-NOR-001 | FR-GW-001, FR-CAN-001 | Gateway와 ECU CAN 통신 확인 | Inventory 응답 수신 |
| TC-NOR-002 | FR-BL-003 | Bootloader에서 App jump 확인 | App 정상 실행 |
| TC-NOR-003 | FR-CAN-002~009 | UDS-style 업데이트 전체 절차 수행 | 새 Slot에 이미지 설치 |
| TC-NOR-004 | FR-AB-004 | 새 App Self-test 및 Confirm | 새 Slot confirmed 처리 |
| TC-NOR-005 | FR-DRV-002, FR-DRV-003 | Drive ECU App v1: B1 버튼 누름 → 직진 → 10cm 장애물 즉시 정지 | 버튼 후 주행 시작, 10cm 이하 장애물 감지 시 즉시 모터 정지, DRIVE_IDLE 복귀 |
| TC-NOR-006 | FR-DRV-004, FR-DRV-007, FR-DRV-008 | App v2 OTA 후: 자동 후진 복귀 동작 확인 및 주행 중 다운로드 + IDLE 활성화 | 장애물 10cm 이하 정지 → 300ms 대기 → 600ms 후진 → DRIVE_IDLE 복귀 순서 동작 확인, OTA 다운로드 완료 후 DRIVE_IDLE 진입 시 자동 재부팅 및 슬롯 전환 확인 |

### 15.2 실패 복구 테스트 (SWE.6 — 시스템 수준)

| 테스트 ID | 관련 요구사항 | 테스트 내용 | 기대 결과 |
|---|---|---|---|
| TC-FAIL-001 | FR-AB-005 | 업데이트 중 전원 차단 | 기존 Confirmed App 부팅 |
| TC-FAIL-002 | FR-CAN-006 | Transfer Data Sequence 누락 | Request Sequence Error 응답 |
| TC-FAIL-003 | FR-BL-012 | 업데이트 중 통신 중단 | Timeout 후 abort |
| TC-FAIL-004 | FR-AB-004 | 새 App Self-test 실패 | 이전 Slot rollback |
| TC-FAIL-005 | FR-FL-004 | Slot 크기 초과 이미지 전송 | Request Out Of Range 또는 Programming Failure |

### 15.3 보안 공격 테스트 (SWE.6 — 시스템 수준)

| 테스트 ID | 관련 요구사항 | 공격 시나리오 | 기대 결과 |
|---|---|---|---|
| TC-SEC-001 | SR-ATK-001 | Firmware 1바이트 변조 | Hash mismatch로 거부 |
| TC-SEC-002 | SR-ATK-002 | unsigned firmware 설치 시도 | Signature verification failure |
| TC-SEC-003 | SR-ATK-003 | 낮은 버전 firmware 설치 | Anti-rollback으로 거부 |
| TC-SEC-004 | SR-ATK-004 | ECU ID 불일치 image 전송 | Target mismatch로 거부 |
| TC-SEC-005 | SR-ATK-005 | 이전 Transfer Data replay | Sequence/Freshness 오류로 거부 |
| TC-SEC-006 | SR-ATK-006 | Security Access 없이 Request Download | Security Access Denied |
| TC-SEC-007 | SR-ATK-007 | Manifest size 초과 Transfer Data | Endless data 방어, 세션 종료 |
| TC-SEC-008 | SR-ATK-008 | CAN Flood 중 업데이트 | Timeout/Abort 후 기존 App 유지 |
| TC-SEC-009 | SR-ATK-009 | Fake complete 명령 전송 | ECU 내부 검증 실패 시 commit 거부 |
| TC-SEC-010 | SR-ATK-010 | 한 ECU만 업데이트 성공 | Campaign partial/fail 기록 |

### 15.4 개발 프로세스 및 디버깅 검증 (SWE.4/SWE.5 — 단위/통합 수준)

| 테스트 ID | 관련 요구사항 | 검증 내용 | 기대 결과 |
|---|---|---|---|
| TC-PRC-001 | PRC-001, RTM-001 | 요구사항 ID 체계 및 RTM 작성 여부 확인 | 주요 요구사항이 설계/구현/테스트와 연결됨 |
| TC-PRC-002 | PRC-003, RTM-002 | 보안 요구사항과 공격 테스트 연결 확인 | 각 보안 요구사항에 관련 TC가 존재함 |
| TC-DBG-001 | DBG-001 | CAN 통신 문제 사례 문서화 | CAN 로그 또는 파형 기반 원인 분석 기록 |
| TC-DBG-002 | DBG-003 | App jump 문제 사례 문서화 | ST-LINK 기반 MSP/VTOR/Reset Handler 확인 기록 |
| TC-DBG-003 | DBG-004 | Flash write 문제 사례 문서화 | Flash error flag 또는 sector boundary 분석 기록 |
| TC-DBG-004 | DBG-006 | 보안 검증 실패 로그 일관성 확인 | 실패 원인과 입력 조건이 문서/로그에 일치함 |

### 15.5 CI/CD 파이프라인 테스트 (SWE.5 — 통합 수준)

| 테스트 ID | 관련 요구사항 | 테스트 내용 | 기대 결과 |
|---|---|---|---|
| TC-CICD-001 | FR-CICD-003, FR-CICD-005 | 빌드 오류 또는 128KB 초과 바이너리 push | 해당 Stage에서 파이프라인 중단 및 오류 로그 기록 |
| TC-CICD-002 | FR-CICD-006, FR-CICD-009 | 정상 빌드 후 서명 및 Manifest 생성 확인 | 아티팩트 저장 확인, Jenkinsfile에 키 평문 없음 확인 |
| TC-CICD-003 | FR-CICD-007, FR-CICD-008 | 전체 파이프라인 실행 후 ECU OTA 완료 확인 | Jenkins 빌드 로그에 ECU Self-test 성공 기록 |

---

## 16. 요구사항 추적성 매트릭스

| 요구사항 그룹 | 대표 ID | 관련 테스트 |
|---|---|---|
| Gateway | FR-GW-001 ~ FR-GW-008 | TC-NOR-001, TC-NOR-003, TC-SEC-010 |
| Bootloader | FR-BL-001 ~ FR-BL-012 | TC-NOR-002, TC-NOR-003, TC-FAIL-001 ~ TC-FAIL-005 |
| A/B Rollback | FR-AB-001 ~ FR-AB-007 | TC-NOR-004, TC-FAIL-001, TC-FAIL-004 |
| CAN / ISO-TP / UDS | FR-CAN-001 ~ FR-CAN-019 | TC-NOR-003, TC-FAIL-002, TC-SEC-005 |
| Drive ECU App | FR-DRV-001 ~ FR-DRV-008 | TC-NOR-005, TC-NOR-006, TC-NOR-007 |
| Sensor ECU App | FR-SEN-001 ~ FR-SEN-005 | TC-NOR-005, TC-NOR-006, TC-NOR-007 |
| Manifest / Firmware Security | SR-MF-001 ~ SR-MF-008, SR-FW-001 ~ SR-FW-006 | TC-SEC-001 ~ TC-SEC-004 |
| Attack Defense | SR-ATK-001 ~ SR-ATK-010 | TC-SEC-001 ~ TC-SEC-010 |
| Uptane-lite | SR-UP-001 ~ SR-UP-005 | TC-NOR-001, TC-SEC-003, TC-SEC-010 |
| Flash Partition | FR-FL-001 ~ FR-FL-005 | TC-NOR-002, TC-FAIL-005 |
| 개발 프로세스 | PRC-001 ~ PRC-007 | TC-PRC-001, TC-PRC-002 |
| 추적성 관리 | RTM-001 ~ RTM-005 | TC-PRC-001, TC-PRC-002 |
| 계측/디버깅 | DBG-001 ~ DBG-006 | TC-DBG-001 ~ TC-DBG-004 |
| CI/CD 파이프라인 | FR-CICD-001 ~ FR-CICD-009 | TC-CICD-001 ~ TC-CICD-003 |

---

## 17. 산출물 요구사항

본 프로젝트의 산출물은 제출 산출물과 엔지니어링 문서 산출물로 구분한다.

### 17.1 제출 산출물

| 산출물 ID | 산출물 | 형식 | 목적 |
|---|---|---|---|
| DEL-001 | 프로젝트 발표 PPT | PPT | 시스템 목적, 구조, 구현 결과, 검증 결과 요약 |
| DEL-002 | 소스코드 저장소 | GitHub | 구현 코드, 빌드 방법, Jenkinsfile, 필수 문서 7종 포함 |
| DEL-003 | 시연 동영상 | 동영상 | OTA 동작, 공격 시나리오 방어, rollback 결과 실증 |
| DEL-004 | 작품소개서 | 문서 | 시스템 개요, 핵심 기능, 보안 설계, 검증 결과 요약 |
| DEL-005 | 개발 작품 하드웨어 | 실물 | 차체, Dual ECU, Gateway, CAN Bus 결합 실물 |

### 17.3 필수 엔지니어링 문서 산출물 7종

본 프로젝트는 ASPICE SWE.1~SWE.6 개발 주기에 대응하는 다음 7종의 문서를 필수 산출물로 작성해야 한다.

| 필수 여부 | 문서 ID | 문서명 | ASPICE 대응 | 목적 |
|---|---|---|---|---|
| 필수 | SRS-001 | Software Requirements Specification | SWE.1 | 프로젝트 범위, 기능/비기능/보안/검증/산출물 요구사항 정의 |
| 필수 | SAD-001 | Software Architectural Design | SWE.2 | 전체 시스템 아키텍처, ECU 역할 분담, 컴포넌트 간 인터페이스, 소프트웨어 레이어 구조 정의 |
| 필수 | SDD-001 | Software Detailed Design | SWE.3 | Bootloader 상태 전이, Flash 파티션, ISO-TP 상태 머신, UDS 서비스별 처리 함수, CAN ID 할당 상세 정의 |
| 필수 | UVR-001 | Unit Verification Report | SWE.4 | cppcheck 정적 분석 결과, 단위 함수 테스트 결과, 코드 리뷰 체크리스트 정리 |
| 필수 | RTM-001 | Requirements Traceability Matrix | SWE.1~6 | 요구사항-설계-구현-테스트-결과 양방향 연결 관리 |
| 필수 | TP-001 | Test Plan | SWE.5/6 | 정상/실패/보안/디버깅 테스트 계획 정의 |
| 필수 | TR-001 | Test Report | SWE.5/6 | 테스트 실행 결과, PASS/FAIL, 로그, 사진/영상 증빙 정리 |
| 필수 | TSR-001 | Troubleshooting Report | SUP.9 | 오실로스코프, 로직분석기, 멀티미터, ST-LINK 기반 문제 분석 및 해결 과정 정리 |

### 17.4 선택 엔지니어링 문서 산출물

필수 7종 외에도 프로젝트 완성도를 높이기 위해 다음 문서는 선택적으로 작성할 수 있다.

| 문서 ID | 문서명 | 목적 |
|---|---|---|
| CAN-001 | CAN / ISO-TP / UDS Message Specification | CAN ID, ISO-TP 프레임, UDS SID/NRC 별도 명세가 필요한 경우 (SDD-001에 포함 가능) |
| SEC-001 | Threat Model & Attack Scenario | 공격자 모델, 공격 시나리오, 방어 요구사항 정의 |
| CHG-001 | Change Log | 요구사항/설계/구현 변경 이력 관리 |
| RN-001 | Release Note | App v1/v2 변경점 및 OTA 결과 정리 |

### 17.5 필수 문서 간 추적 관계

필수 문서 7종은 다음 관계를 가져야 한다.

```text
SRS-001  (SWE.1)
  ↓ 요구사항 ID → 컴포넌트 할당
SAD-001  (SWE.2)
  ↓ 아키텍처 컴포넌트 → 상세 설계
SDD-001  (SWE.3)
  ↓ 함수/모듈/프로토콜 명세 → 구현
  ↕ 요구사항-설계-구현-테스트 양방향 연결
RTM-001
  ↓ 테스트 케이스 연결
TP-001   (SWE.5/6)
  ↓ 테스트 케이스 실행
TR-001   (SWE.5/6)
  ↓ 실패/비정상 현상 발생 시
TSR-001  (SUP.9)
  ↓ 원인 분석 및 재발 방지 → SRS/SAD/SDD/RTM 반영
```

| 연결 기준 | 요구사항 |
|---|---|
| SRS → SAD | 모든 주요 기능/보안 요구사항은 SAD의 컴포넌트 또는 인터페이스 항목으로 할당되어야 한다. |
| SAD → SDD | 아키텍처의 각 컴포넌트는 SDD에서 상태 전이, 함수, 자료구조 수준으로 상세화되어야 한다. |
| SDD → RTM | SDD의 설계 항목은 RTM에서 요구사항 ID 및 구현 파일/함수와 연결되어야 한다. |
| RTM → TP | 각 핵심 요구사항은 최소 1개 이상의 테스트 케이스와 연결되어야 한다. |
| TP → TR | 테스트 케이스는 실행 결과, 로그, PASS/FAIL, 증빙 자료와 연결되어야 한다. |
| TR → TSR | 테스트 중 발생한 주요 실패 또는 비정상 현상은 TSR에 원인 분석과 해결 조치로 기록되어야 한다. |
| TSR → SRS/RTM | 트러블슈팅 결과 요구사항 또는 설계 변경이 필요하면 SRS/SAD/SDD/RTM에 반영되어야 한다. |

---

## 18. 성공 기준

본 프로젝트는 다음 조건을 만족하면 1차 성공으로 판단한다.

1. Raspberry Pi 5와 STM32F446RE 2대가 CAN으로 통신한다.
2. 각 ECU가 Custom Bootloader를 통해 App으로 정상 jump한다.
3. Gateway가 UDS over ISO-TP 절차로 각 ECU에 Full Image OTA를 수행한다.
4. 새 이미지는 비활성 Slot에 기록된다.
5. Hash, Signature, Version, ECU ID 검증이 수행된다.
6. 검증 성공 후 새 Slot으로 부팅하고 Self-test 후 Confirmed 처리된다.
7. 업데이트 실패 또는 전원 차단 시 기존 Confirmed Slot으로 복구된다.
8. Drive ECU App v1(즉시 정지)/v2(자동 후진 복귀)의 장애물 반응 동작 차이가 확인된다.
9. Sensor ECU의 장애물 거리 메시지(CAN 0x200)가 Drive ECU의 g_distance_cm에 반영된다.
9-1. OTA 다운로드 완료 후 펌웨어 활성화가 DRIVE_IDLE 상태에서 자동으로 수행된다. (Uptane 지연 활성화)
10. 변조, unsigned, downgrade, replay, unauthorized update, CAN flood 공격 시나리오가 거부 또는 안전 실패 처리된다.
11. 요구사항-설계-구현-테스트 추적성 문서와 테스트 로그가 남는다.
12. 주요 디버깅 이슈는 사용 도구, 측정 근거, 원인, 조치, 재검증 결과를 포함하여 TSR-001에 기록된다.

---

## 19. 한계 및 명시적 인정 사항

본 시스템은 가용 개발 플랫폼(STM32F446RE, Raspberry Pi 5) 기반의 축소형 전장 OTA 검증 시스템이다. 다음 한계 사항을 명시한다.

1. STM32F446RE는 실제 차량용 HSM MCU가 아니므로 양산 ECU 수준의 키 보호와 Secure Boot ROM을 제공하지 않는다.
2. F446RE는 하드웨어 Dual Bank Flash 구조가 아니므로, 단일 Flash를 논리적으로 분할한 A/B Slot 구조를 사용한다.
3. UDS는 정식 상용 UDS 스택 전체 구현이 아니라, 리프로그래밍 절차를 참고한 UDS-style 구조이다.
4. Uptane은 전체 표준 구현이 아니라, Manifest, ECU Inventory, Version Report, Rollback 방어, Partial Bundle 방어 개념을 참고한 Uptane-lite 구조이다.
5. Delta Update는 1차 구현 범위에서 제외하고, Full Image 기반 A/B Secure OTA에 집중한다.
6. ASPICE는 공식 준수 또는 평가가 아니라, 요구사항 기반 개발과 추적성 개념을 참고한 수준으로 적용한다.
7. 공격자 시나리오는 자체 제작한 CAN 테스트 네트워크 내부에서만 수행한다.
8. 실제 차량의 IGN 상태, 배터리 전압 조건, DTC, 네트워크 슬립/웨이크업, 서비스센터 복구 정책은 1차 범위에서 제외한다.
9. **직진 주행 안정화 불가 (개루프 제어).** 실 테스트에서 매 실행마다 진행 방향이 좌·우로 불규칙하게 변하는 현상이 확인된다. **근본 원인은 휠 엔코더·IMU 부재로 인한 개루프(open-loop) 차동 구동**이다 — 모터/기구 편차나 휠 슬립이 발생해도 주행거리·헤딩 피드백이 없어 *실시간 보정이 불가능*하다. 휠 엔코더(주행거리)+IMU(헤딩) 입고 시 폐루프 헤딩 제어(PID 등)로 직진 안정화가 가능하다(§19.1 L-7). 단 본 데모의 핵심(Secure OTA·A/B 슬롯 전환·센서 staleness fail-safe에 의한 장애물 정지)은 직진성과 독립이며 정상 동작한다.

이러한 한계를 인정한 상태에서, 본 프로젝트는 개인이 구현 가능한 범위 내에서 Secure OTA의 핵심 설계 사고와 전장 펌웨어 개발 역량을 증명하는 것을 목표로 한다.

### 19.1 한계 및 잔여 위험 레지스터 (Known Limitations & Residual Risk)

§19의 한계 중 **구체적 해소 경로(후속 트리거)를 가진 항목**을 한 곳에 모아 추적한다. 각 항목은 *현재 왜 수용 가능한가(완화)* · *무엇이 들어오면 해소되는가(트리거)* · *근거 ADR*을 함께 명시한다. 이는 ISO/SAE 21434의 **잔여 위험 수용(residual risk acceptance)** 과 ASPICE 추적성(SWE.6) 원칙을 따른 것으로, 해당 항목이 "미구현"이 아니라 **근거 있는 범위 결정**임을 기록한다. 보안 항목 대부분은 **Secure Element(ATECC608A, SparkFun DEV-18077 — 주문 완료·입고 대기)** 통합 한 번으로 해소된다.

| ID | 범주 | 한계 | 영향 요구사항 | 현재 영향 / 완화 (왜 지금 수용 가능한가) | 후속 해소 트리거 | 근거 |
|---|---|---|---|---|---|---|
| L-1 | 보안 | Replay 방어(session/sequence/freshness) 미구현 | FR-CAN-017(Should), SR-ATK-005 | 정상 OTA는 SecurityAccess(HMAC-SHA256) 인증 + CAN 물리 접근 필요 → 원격 무인증 replay 차단. 단 세션 내 freshness 카운터는 부재 | ATECC608A TRNG로 강한 seed freshness 확보 후 구현 | ADR-004, ADR-006 |
| L-2 | 보안 | `hardware_id` 호환성 검사 미구현(`target_ecu_id`만 강제) | FR-CAN-011(Must) | 단일 보드종(F446) + 2-ECU 구성에서 `target_ecu_id`(Drive/Sensor)가 식별을 충족 → 핵심 위협(타 ECU 이미지 설치) 차단. HW revision 검사는 현 구성에선 잉여 | 다(多)-보드/리비전 확장 또는 ATECC608A 프로비저닝 시 추가 | ADR-009 |
| L-3 | 보안 | SecurityAccess 잠금 RAM 기반(전원 리셋 시 해제) | FR-CAN-010 | 10s 잠금 + HMAC Key 필요, 리셋마다 seed 재발급되어 무차별 대입 비용 존재. 단 리셋으로 잠금 카운터 우회 가능 | ATECC608A monotonic counter로 NV 잠금 | ADR-003, ADR-006 |
| L-4 | 보안 | Seed = SW nonce(TRNG 부재, 약 엔트로피) | FR-CAN-010, §13.6 | UID+SysTick+카운터 SHA-256 혼합 — 원격 위협모델엔 충분, 물리 관측·재부팅 replay는 모델 밖 | ATECC608A TRNG → HMAC_DRBG seed | ADR-004, ADR-006 |
| L-5 | 보안 | Anti-rollback 기준선 = 메타 NV(CRC-only) | FR-BL-008, FR-AB-008 | 원격 다운그레이드 차단; CRC는 우발적 손상 검출용이라 *물리 위조*만 잔여 위험 | ATECC608A 보호 슬롯에 기준선 저장 | ADR-007, ADR-006 |
| L-6 | 보안 | ECU-id 강제 = 앱 레벨만(UDS 우회 직접 플래시 미차단) | FR-CAN-011, FR-AB-008 | 원격 OTA 경로 차단(0x37 NRC 0x31); 직접 플래시는 물리 접근 필요(이미 더 큰 위협) | 부트로더 점프 직전 재검사(defense-in-depth) + per-ECU 정체성(ATECC608A) | ADR-009, ADR-006 |
| L-7 | 제어/안전 | 직진 주행 안정화 불가(휠 엔코더·IMU 부재 → 개루프) | FR-DRV-001, §19(9) | 주행거리·헤딩 피드백 부재로 실시간 보정 불가 → 실행마다 좌·우 편차. **단 Secure OTA·A/B 전환·센서 staleness fail-safe(장애물 정지)는 직진성과 독립이며 정상 동작** | 휠 엔코더(주행거리) + IMU(헤딩) 입고 시 폐루프 헤딩 제어(PID 등) | §19(9) |

> 위 항목은 모두 **두 하드웨어 입고 이벤트**(ATECC608A / 엔코더·IMU)로 해소되는 *하드웨어 의존 후속*이며, 각 근거 ADR에 상세 설계·트레이드오프가 기록돼 있다. 입고 지연으로 본 1차 범위에서는 위와 같이 잔여 위험을 명시·수용한다.

---

## 20. 향후 확장 방향

1차 구현 이후 다음 확장을 고려할 수 있다.

| 확장 항목 | 목적 |
|---|---|
| ISO-TP 멀티 세션 확장 | 복수 ECU 동시 업데이트 세션 관리 구현 |
| Delta Update | CAN 전송량과 업데이트 시간을 줄이는 최적화 실험 |
| 차량용 MCU 보드 포팅 | AURIX, S32K, SPC5 등 차량용 MCU 경험 확장 |
| HSM/Secure Element 연동 | 키 저장과 서명 검증 보안성 강화 |
| CANoe/CANalyzer 또는 대체 도구 연동 | 테스트 자동화와 CAN 로그 분석 전문성 강화 |
| DTC/진단 서비스 추가 | OTA 실패 원인과 ECU 상태를 진단 서비스로 조회 |
| 배터리 전압/주행 상태 조건 추가 | 실제 차량 OTA 조건에 가까운 업데이트 조건 제어 |


---

## 21. 개발 일정

### 21.1 전체 일정 요약

| 주차 | 기간 | 주요 목표 | 완료 마일스톤 |
|---|---|---|---|
| 1주차 | 2026-05-16 ~ 2026-05-22 | 개발 환경, Jenkins CI/CD 기반, Custom Bootloader | Bootloader → App jump 성공 |
| 2주차 | 2026-05-23 ~ 2026-05-29 | CAN/ISO-TP/UDS OTA 파이프라인, Secure OTA | 전체 OTA 흐름 동작 (Gateway → ECU 플래시 → 재부팅) |
| 3주차 | 2026-05-30 ~ 2026-06-06 | App v1/v2 구현, 보안 테스트, Jenkins 완성, 문서화 | 전체 시스템 통합 및 필수 문서 7종 완성 |

### 21.2 주차별 상세 일정

#### 1주차: 개발 환경 + Bootloader (2026-05-16 ~ 2026-05-22)

| 날짜 | 작업 내용 | 산출물 |
|---|---|---|
| 05/16 ~ 05/17 | Jenkins RPi5 설치, arm-none-eabi-gcc 크로스컴파일 Stage 구성 (FR-CICD-001~003) | Jenkinsfile Stage 1~3 동작 |
| 05/18 ~ 05/19 | STM32 Flash 파티션 설계, Linker Script 작성, 메모리맵 확정 (FR-FL-001~002) | 파티션 레이아웃 확정 |
| 05/20 ~ 05/22 | Custom Bootloader 구현: App jump, Vector Table 재배치, Boot Metadata R/W (FR-BL-001~006) | Bootloader → App jump 성공 |

#### 2주차: OTA 파이프라인 + Secure OTA (2026-05-23 ~ 2026-05-29)

| 날짜 | 작업 내용 | 산출물 |
|---|---|---|
| 05/23 ~ 05/24 | CAN 드라이버 + ISO-TP SF/FF/CF/FC 수신 상태머신 (FR-CAN-002~007) | ISO-TP 수신 동작 확인 |
| 05/25 ~ 05/26 | UDS 명령 처리 구현 (0x10, 0x27, 0x34, 0x36, 0x37, 0x31, 0x11) (FR-CAN-008~015) | UDS 전체 세션 흐름 동작 |
| 05/27 ~ 05/28 | SHA-256 무결성 검증, ECDSA 서명 검증, Anti-rollback 구현 (SR-FW-001~003) | 변조 이미지 거부, 서명 검증 통과 |
| 05/29 | A/B Slot 전환 및 Rollback 동작 검증, 전원 차단 복구 테스트 (FR-AB-001~007) | TC-FAIL-001, TC-FAIL-004 PASS |

#### 3주차: App 구현 + 통합 + 문서화 (2026-05-30 ~ 2026-06-06)

| 날짜 | 작업 내용 | 산출물 |
|---|---|---|
| 05/30 ~ 05/31 | Drive ECU App v1(즉시 정지) + App v2(자동 후진 복귀) 구현, 장애물 임계값 10cm 고정 (FR-DRV-001~008, FR-SEN-002~004) | v1/v2 버튼 트리거 주행 및 장애물 반응 동작 확인 |
| 06/01 | OTA로 App v1→v2 순차 배포 및 장애물 반응 차이 실증, Uptane 지연 활성화 확인 (FR-DRV-007, FR-DRV-008) | TC-NOR-005~006 PASS |
| 06/02 ~ 06/03 | 보안 공격 시나리오 테스트 10종 수행 (SR-ATK-001~010) | TC-SEC-001~010 PASS |
| 06/04 | Jenkins Stage 4~5 완성 (서명 자동화 + OTA 자동 배포) (FR-CICD-006~009) | TC-CICD-001~003 PASS |
| 06/05 ~ 06/06 | 필수 문서 7종 완성 (SAD, SDD, RTM, TP, TR, TSR) + 최종 검토 | 필수 문서 7종 완료 |

### 21.3 마일스톤 요약

| 마일스톤 ID | 날짜 | 내용 |
|---|---|---|
| MS-001 | 2026-05-22 | Bootloader에서 App jump 성공, Jenkins 빌드 파이프라인 동작 |
| MS-002 | 2026-05-29 | 전체 OTA 흐름 성공 (SHA-256/ECDSA 검증 포함), Rollback 동작 확인 |
| MS-003 | 2026-06-01 | App v1/v2 OTA 실증 완료 (버튼 트리거 주행 + 장애물 반응 단계별 차이 확인, Uptane 지연 활성화 확인) |
| MS-004 | 2026-06-04 | 보안 공격 시나리오 10종 통과, Jenkins 전체 파이프라인 완성 |
| MS-005 | 2026-06-06 | 필수 문서 7종 완성, 프로젝트 1차 완료 |

---

## 22. 용어 정의

| 용어 | 정의 |
|---|---|
| OTA | Over-The-Air. 네트워크를 통한 소프트웨어/펌웨어 업데이트 |
| ECU | Electronic Control Unit. 차량 내 전자제어장치 |
| Gateway | 여러 ECU와 외부 업데이트 서버 사이에서 업데이트를 중계하는 노드 |
| Bootloader | Reset 후 먼저 실행되어 App 검증, 업데이트, App jump를 담당하는 펌웨어 |
| App Slot | 실행 가능한 Application Image가 저장되는 Flash 영역 |
| A/B Update | 기존 App을 유지한 채 비활성 Slot에 새 App을 설치하고 검증 후 전환하는 업데이트 방식 |
| Rollback | 새 App 실패 시 이전 정상 App으로 복구하는 동작 |
| Manifest | 업데이트 대상, 버전, 크기, Hash, Signature 등을 담은 메타데이터 |
| Hash | 데이터 무결성 검증을 위한 요약값 |
| Signature | 펌웨어가 신뢰된 배포자로부터 왔음을 확인하기 위한 전자서명 |
| Anti-rollback | 취약한 구버전 펌웨어 재설치를 방지하는 정책 |
| UDS | Unified Diagnostic Services. 차량 진단 서비스 표준 |
| UDS-style | 본 프로젝트에서 UDS 개념을 참고하여 축소 구현한 명령 구조 |
| Uptane-lite | Uptane의 메타데이터/위협 모델 개념을 참고한 축소형 OTA 보안 구조 |
| CAN | Controller Area Network. 차량용 네트워크 프로토콜 |
| DTC | Diagnostic Trouble Code. 진단 고장 코드 |
| ASPICE-inspired | Automotive SPICE를 공식 준수하지는 않지만 요구사항 기반 개발, 추적성, 검증 연계 개념을 참고하는 방식 |
| RTM | Requirements Traceability Matrix. 요구사항-설계-구현-테스트 연결표 |
| TSR | Troubleshooting Report. 문제 발생, 분석, 조치, 재검증 과정을 기록한 문서 |
| VTOR | Vector Table Offset Register. Cortex-M에서 인터럽트 벡터 테이블 위치를 지정하는 레지스터 |

---

## 23. 개정 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| 1.0 | 2026-05-15 | 초기 Secure OTA SRS 작성 |
| 1.1 | 2026-05-15 | 라인트레이싱 차량 실증, 논리적 A/B Slot, Uptane-lite, 공격 시나리오, Delta Update 제외 범위 반영 |
| 1.2 | 2026-05-15 | ASPICE-inspired 개발 관리, 요구사항 추적성, 계측기 기반 트러블슈팅, 디버깅 검증 요구사항 반영 |
| 1.3 | 2026-05-15 | 최종 제출 산출물 5종과 필수 문서 산출물 5종(SRS, RTM, TP, TR, TSR) 요구사항 반영 |
| 1.4 | 2026-05-16 | ISO-TP 전송 계층 추가, SAD-001/SDD-001 필수 문서 추가(필수 문서 7종 확장), 산출물 요구사항 간소화, Drive ECU v1/v2 조작변인 구체화(ON/OFF → 비례 제어), Sensor ECU v1/v2 임계값 구체화(15cm → 25cm), HC-SR04 확정 |
| 1.5 | 2026-05-16 | Jenkins 기반 CI/CD 파이프라인 요구사항 추가(FR-CICD-001~009), 시스템 구성 다이어그램 업데이트, CI/CD 테스트 케이스 추가(TC-CICD-001~003), RTM 업데이트, Section 21 개발 일정 신규 추가(3주 일정, 마일스톤 5종) |
| 1.6 | 2026-05-16 | CAN ID 테이블 추가(Section 13.2), ISO-TP/Security Access 파라미터 수치화(Section 6.2, 13.6), 키 관리 요구사항 추가(SR-KEY-001~004), IWDG 8000ms/WRP(FR-BL-012~013), Safe State 정의(NFR-SAFE-004), NFR-PERF-003 ≤10ms 수치화, UVR-001(SWE.4) 필수 문서 추가, TC SWE 레벨 구분, UN R156 Out of Scope 명시 |
| 2.0 | 2026-05-25 | 주행 방식 전면 변경(라인트레이싱 → 버튼 트리거 직진): FR-DRV 전체 재작성(001~008), 라인센서 제거, B1 USER 버튼 추가, App v1/v2/v3 3단계 OTA 구조 확정(즉시 정지/비례 감속/자동 후진), Uptane 지연 활성화 도입(FR-DRV-007, FR-DRV-008, FR-CICD-007 갱신), 장애물 임계값 단일화 10cm(FR-SEN-002 갱신, FR-SEN-004 재정의), NFR-SAFE-001 갱신(다운로드 중 주행 허용), 성공 기준 §18 item 8~9 갱신, TC-NOR-005~007 갱신, 시스템 개요 §3.1/3.2 갱신 |
| 2.1 | 2026-05-29 | App 버전 구조 변경: 비례 감속(v2) 제거, 자동 후진 복귀를 v2로 통합. FR-DRV-004 갱신(v2=자동 후진 복귀), FR-DRV-005 삭제, FR-SEN-004 수용 기준 갱신, NFR-MNT-004 갱신, 성공 기준 §18 item 8~9 갱신, TC-NOR-006~007 통합(TC-NOR-006), 일정 §21.2~21.3 갱신 |
| 2.2 | 2026-06-01 | §7.3 A/B Slot 강화(적대적 질문 리뷰 반영): 슬롯 5상태 생명주기 모델 도입(INVALID/UPDATING/UPDATED/TRIAL/CONFIRMED, ISO 24089·AUTOSAR UCM 근거, §7.3.1), FR-AB-001~007 재작성, 서명 Image Header 신규(FR-AB-008), Self-test 측정가능 4항목·Tboot 실측 유도화, fail-closed 검증(image_size==0 우회 차단). 메타데이터 원자적 이중화(섹터 2·3 redundant + CRC32 + seq_counter, FR-AB-005/006, §13.3·§14.2 갱신). Boot 타이밍 측정 방식 결정 ADR-002 추가 |
| 2.3 | 2026-06-02 | SecurityAccess HMAC 구현 반영(D1): §13.6 정정 — F446 무RNG→소프트웨어 DRBG seed, `HMAC-SHA256(key=PSK, msg=Seed)[0:4]` 명확화, PSK를 WRP Bootloader `0x08007FE0` 배치(SR-KEY-003), 3회→NRC 0x36 + 10s→NRC 0x37 잠금(RAM 기반, NV는 후속). 양 ECU·게이트웨이·단위테스트(test_hmac/test_uds_state) 일치 검증 |
| 2.4 | 2026-06-02 | §13.6 Seed 생성 표기 정정 — "소프트웨어 DRBG"는 부정확(엔트로피원 없음)하여 "소프트웨어 nonce"로 정정, NIST SP 800-90 미준수·재부팅 replay 한계 명시 및 개선 경로(ADC/발진기 지터→HMAC_DRBG, HSM/SHE) ADR-004 추가 |
| 2.5 | 2026-06-03 | 문서 정합성 패스: WRP 영역 정정(FR-BL-013·SR-KEY-001 "Sector 0~4"→"0~1" — 0~4는 Metadata·Slot A까지 잠가 OTA 불가, §14.2와 모순 해소), 파일명 버전표기 제거(SSOT — 버전은 본 개정이력에만), 메타데이터 이중화(섹터2·3)를 README·TEST_SPEC 메모리맵에 반영, TEST_SPEC 추적성 §참조→FR/SR ID화, SecurityAccess XOR 잔재→HMAC 정정(TEST_SPEC·diagram) |
| 2.6 | 2026-06-03 | §7.4 UDS 프로토콜 강화(적대적 질문 Round 2): FR-CAN-009(programmingSession 0x02 명시)·010(HMAC(key=PSK,msg=Seed)·nonce 정합 ADR-004)·011(ecu_id/hw_id·version fail-fast)·012(endless-data 누적상한 SR-ATK-007)·013(수신완료 검증)·014(fail-closed Verify→UPDATED) 갱신, FR-CAN-018(RDBID ECU Inventory)·019(S3 세션 타임아웃) 신설 |
| 2.7 | 2026-06-03 | self-test commit + 3-strike 롤백 구현(FR-AB-004/007) + 슬롯 5상태 정합: 코드의 SLOT_PENDING을 UPDATED/TRIAL/UPDATING로 분리(§7.3.1 5상태 실코드화), ota_meta_plan_boot(증가-먼저-점프·3회 초과 INVALID+롤백)·plan_confirm(TRIAL→CONFIRMED) 추가, 부트로더 bl_meta_commit, 앱 self-test(heartbeat 성공)→ota_meta_self_confirm, §13.3·FR-BL-002·FR-AB-006의 pending 어휘 5상태 정정. 단위테스트 48개 통과 |
| 2.8 | 2026-06-03 | anti-rollback(FR-BL-008/FR-AB-008) 구현 — ADR-007 추가(앞 서명 헤더, MCUboot 정석). 서명 이미지 `[헤더0x200][코드][서명64]`, ECDSA가 (헤더+코드) 덮음, 앱 링커 origin +0x200, 부트로더 점프/검증 +0x200·ECDSA 후 헤더 fw_version을 메타 CONFIRMED 버전과 비교해 다운그레이드 거부. sign_firmware.py `--version`, write_pending이 헤더 fw_version 기록. 구현은 헤더 subset(magic/fw_version/target_ecu_id) — header_version/hw_id/hash·Manifest 출처는 후속. 단위테스트 anti_rollback 6 신규(총 54), CRC 기준선·HIL 부팅 검증 한계는 ADR-007 기록 |
| 2.9 | 2026-06-03 | ECDSA 검증 게이팅 fail-closed 구현(FR-AB-003, CWE-636) — 기존 부트로더는 메타 size가 비정상(0/초과/0xFFFFFFFF)이면 ECDSA를 *건너뛰고 부팅*(fail-open 우회). `bootloader_verify_decision`(순수) 도입: 메타 유효+size 비정상 → BL_VERIFY_REFUSE→safe_state(부팅 거부), 메타 부재(factory/ST-Link)만 SKIP, 정상 size만 REQUIRED. 검증 *우회* 대신 *부팅 거부*로 deny-by-default 충족. test_bootloader_slot 7 신규(총 15, 누적 61) |
| 2.10 | 2026-06-05 | ECU 식별 강제 구현(FR-CAN-011) — 서명 헤더 `target_ecu_id`가 정의·서명만 되고 *아무도 거부하지 않던* 갭 해소. 앱 컴파일타임 ID(`OTA_ECU_ID` Drive=1/Sensor=2)로 RequestTransferExit(0x37)에서 헤더 ecu_id≠자기ID면 NRC 0x31 거부(메타 commit 전). FR-CAN-011은 0x34 *요청필드* 검사를 상정했으나, 구현은 *서명 헤더*(위조불가)를 0x37에서 검사해 의도를 더 강하게 충족. 순수함수 `ota_meta_ecu_id_allowed`(SSOT 3곳), test_anti_rollback 4 신규(총 10, 누적 78). UDS 우회(직접플래시) 차단=부트로더 defense-in-depth 후속, ADR-009 |
| 2.11 | 2026-06-05 | **한계·잔여위험 통합 레지스터 신설(§19.1)** — 흩어져 있던 후속 항목을 한 표로 색인(ISO/SAE 21434 잔여위험 수용·ASPICE SWE.6): 보안 6항목(L-1 replay·L-2 hardware_id·L-3 잠금NV·L-4 seed·L-5 anti-rollback 기준선·L-6 ECU-id 앱레벨 — 대부분 ATECC608A 입고로 해소) + 제어 1항목(L-7 직진 안정화 — 엔코더·IMU 입고로 해소)을 *현재완화·후속트리거·근거ADR*과 함께 명시. FR-CAN-011에 `hardware_id` 후속 이연 표기, §19(9) 직진 이슈 근본원인 규명(엔코더·IMU 부재→개루프, 폐루프 제어로 해소). README에 레지스터 포인터 추가 |

