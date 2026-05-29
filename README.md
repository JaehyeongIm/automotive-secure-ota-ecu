# Automotive Secure OTA ECU

CAN 버스 기반 Dual ECU 환경에서 **UDS over ISO-TP Secure OTA 파이프라인**을 구현한 임베디드 시스템 프로젝트.  
STM32F446RE 2대를 대상 ECU로, Raspberry Pi 5를 OTA Gateway 겸 Jenkins CI/CD 서버로 구성해 전장 OTA의 핵심 기능을 실물 RC 차량으로 실증합니다.

---

## 시스템 구성

```
[개발자 PC]
     │ git push
     ▼
[Raspberry Pi 5] ── Jenkins CI/CD (단위 테스트 → 정적 분석 → 크로스컴파일 → 서명 → OTA)
     │              └── OTA Gateway (ECU Inventory, IDLE 감지 후 전송)
     │ CAN Bus (500 Kbps)
     ├──────────────────────────────────┐
     ▼                                  ▼
[DriveECU]                         [SensorECU]
 STM32F446RE                        STM32F446RE
 Custom Bootloader                  Custom Bootloader
 A/B Slot                           A/B Slot
 버튼 트리거 직진 주행               HC-SR04 장애물 감지
 TB6612FNG 모터 제어                상태 메시지 송신
```

---

## 구현 내용

### Custom Secure Bootloader

- ECDSA-P256 서명 검증 후 App jump (uECC 라이브러리 직접 포팅)
- A/B Slot — 비활성 슬롯에만 기록, 검증 실패 시 반대 슬롯 자동 Fallback
- 부팅 시도 3회 초과 시 Rollback, Boot Metadata CRC 검증, IWDG Watchdog
- Bootloader 영역 STM32 WRP(Write Protection) 하드웨어 잠금

### OTA 보안

| 항목 | 구현 |
|---|---|
| 무결성 | SHA-256 이미지 해시 검증 |
| 인증성 | ECDSA-P256 서명 검증 |
| Anti-rollback | firmware_version 비교, 다운그레이드 거부 |
| Security Access | HMAC-SHA256(Seed ‖ PSK) 기반 Key 인증, 3회 실패 시 10초 잠금 |
| ECU 식별 | target_ecu_id / hardware_id 불일치 이미지 거부 |
| Replay 방어 | Session ID, Sequence Number, Freshness Counter |
| Uptane-lite | Manifest 기반 검증, ECU Inventory, Campaign 단위 결과 관리 |

### UDS over ISO-TP (CAN Classic 500 Kbps)

ISO-TP SF/FF/CF/FC 프레임 처리 및 UDS 상태머신을 C로 직접 구현.

```
0x10 DiagnosticSessionControl → 0x27 SecurityAccess
→ 0x34 RequestDownload → 0x36 TransferData (256 B/chunk)
→ 0x37 RequestTransferExit → ECUReset
```

### Uptane 지연 활성화

Gateway가 CAN heartbeat의 `driving_state`를 모니터링하여 ECU가 IDLE 상태일 때만 OTA 전송을 시작합니다. Flash Erase 중 CPU 블로킹으로 인한 주행 중 위험을 원천 차단하며, Uptane 표준의 "안전 조건 확인 후 설치" 원칙을 구현합니다.

### Jenkins CI/CD 파이프라인

git push 한 번으로 ECU 슬롯 전환 확인까지 자동 수행. 변경된 ECU만 선택적으로 빌드/배포.

1. **단위 테스트** — `ceedling test:all`, 실패 시 이후 단계 전부 차단
2. **정적 분석** — `cppcheck` error 등급 이상 시 중단
3. **변경 ECU 감지** — `git diff`로 DriveECU / SensorECU 구분
4. **크로스컴파일** — `arm-none-eabi-gcc`, 슬롯별 링커스크립트 적용
5. **바이너리 크기 검사** — Slot 한계(128 KB) 초과 시 중단
6. **서명** — ECDSA 개인키는 Jenkins Credentials로만 관리
7. **OTA 배포** — ECU IDLE 확인 후 UDS/ISO-TP 전송, heartbeat로 슬롯 전환 검증

---

## Flash 메모리 맵 (STM32F446RE)

```
0x08000000  Bootloader  (Sector 0–4, WRP 보호)
0x08008000  Boot Metadata
0x08010000  Slot A App  (~192 KB)
0x08040000  Slot B App  (~256 KB)
```

---

## CAN ID

| CAN ID | 방향 | 용도 |
|---|---|---|
| 0x7E0 / 0x7E8 | Gateway ↔ DriveECU | UDS 진단 요청 / 응답 |
| 0x7E1 / 0x7E9 | Gateway ↔ SensorECU | UDS 진단 요청 / 응답 |
| 0x100 | DriveECU → Bus | 상태 보고 (버전, 슬롯, 주행 상태) |
| 0x200 | SensorECU → Bus | 장애물 감지 + 거리값 |
| 0x201 | SensorECU → Bus | Heartbeat (100 ms 주기) |

---

## 하드웨어

| 부품 | 역할 |
|---|---|
| Raspberry Pi 5 | OTA Gateway, Jenkins CI/CD 서버 |
| STM32F446RE × 2 | Drive ECU, Sensor/Body ECU |
| SN65HVD230 | CAN Transceiver |
| CANable (USB-CAN) | RPi ↔ CAN 버스 연결 |
| TB6612FNG | DC 모터 드라이버 |
| HC-SR04 | 초음파 거리 센서 |
| 2WD RC 차체 | OTA 적용 결과 실증 플랫폼 |

---

## 디렉터리 구조

```
├── Bootloader/          STM32 Custom Secure Bootloader
├── DriveECU/            Drive ECU 펌웨어 (App v1/v2, Slot A/B 링커)
├── SensorECU/           Sensor/Body ECU 펌웨어
├── test/                Ceedling 단위 테스트 (Bootloader, OTA 메타, UDS 상태머신)
├── tools/
│   ├── ota_client.py    UDS/ISO-TP OTA 전송 클라이언트
│   ├── sign_firmware.py ECDSA-P256 서명 도구
│   └── can_monitor.py   CAN 프레임 모니터링
├── ci/
│   ├── build.sh         크로스컴파일 스크립트
│   └── read_slot.py     CAN heartbeat에서 활성 슬롯 읽기
├── Jenkinsfile          CI/CD 파이프라인 정의
└── docs/
    ├── SRS-001_CAN_Secure_OTA_Pipeline_v1.4.md
    ├── ADR-001_OTA_Activation_Architecture.md
    ├── TEST_SPEC_OTA_v1.0.md
    └── diagram.md       Context / Block / State / Sequence 다이어그램
```

---

## DriveECU App 버전

| 버전 | 동작 |
|---|---|
| v1 | 버튼 트리거 직진 주행, 10 cm 이내 장애물 감지 시 즉시 정지 |
| v2 | v1 로직 + 정지 후 자동 후진 복귀 (300 ms 대기 → 600 ms 후진) |

---

## 테스트 실행

```bash
# 의존성 설치 (Raspberry Pi)
sudo apt install -y ruby-full gcc-arm-none-eabi cppcheck
gem install ceedling
pip install python-can

# 단위 테스트 + OTA 통합 테스트 (3라운드)
python3 ci/test_all.py --channel can0 --key <개인키>

# Jenkins E2E 시연: App 버전 수정 후 push
# → GitHub Webhook → Jenkins → 단위 테스트 → cppcheck → 빌드 → 서명 → OTA → 슬롯 전환 확인
git add DriveECU/Core/Inc/drive.h
git commit -m "feat: DriveECU app v2"
git push origin main
```

---

## 알려진 문제

| 항목 | 상태 | 설명 |
|---|---|---|
| 직진 주행 미보장 | 미해결 | 매 실행마다 진행 방향이 좌·우 불규칙하게 달라짐. 원인(모터 PWM 타이밍·기구적 요인) 분석 필요. |

---

## 문서

- [SRS-001](docs/SRS-001_CAN_Secure_OTA_Pipeline_v1.4.md) — 소프트웨어 요구사항 명세서
- [ADR-001](docs/ADR-001_OTA_Activation_Architecture.md) — OTA 활성화 아키텍처 의사결정
- [TEST_SPEC](docs/TEST_SPEC_OTA_v1.0.md) — 소프트웨어 테스트 명세서
- [diagram](docs/diagram.md) — 시스템 다이어그램 (Context / Block / State / Sequence)
