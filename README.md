# Automotive Secure OTA ECU

CAN 버스 기반 Dual ECU 환경에서 **UDS over ISO-TP Secure OTA 파이프라인**을 구현한 임베디드 시스템 프로젝트입니다.  
Raspberry Pi 5를 OTA Gateway 겸 Jenkins CI/CD 서버로, STM32F446RE 2대를 대상 ECU로 구성하여 전장 OTA의 핵심 기능을 실증합니다.

---

## 시스템 구성

```
[개발자 PC]
     │ git push
     ▼
[Raspberry Pi 5] ── Jenkins CI/CD (크로스컴파일 → 정적분석 → 서명 → 배포)
     │              └── OTA Gateway (ECU Inventory, Campaign 관리)
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

## 주요 기능

### Custom Secure Bootloader
- ECDSA-P256 서명 검증 후 App jump
- A/B Slot 기반 업데이트 — 비활성 슬롯에만 기록, 현재 실행 중인 슬롯은 보호
- Self-test 미통과 시 자동 Rollback (부팅 시도 횟수 3회 제한)
- Boot Metadata CRC 검증, IWDG Watchdog 적용
- Bootloader 영역(Sector 0–4) STM32 WRP(Write Protection) 설정

### OTA 보안
| 항목 | 구현 |
|---|---|
| 무결성 | SHA-256 이미지 해시 검증 |
| 인증성 | ECDSA-P256 서명 검증 (공개키 Bootloader에 하드코딩) |
| Anti-rollback | firmware_version 비교, 다운그레이드 거부 |
| Security Access | HMAC-SHA256(Seed \|\| PSK) 기반 4바이트 Key 인증, 3회 실패 시 10초 잠금 |
| ECU 식별 | target_ecu_id / hardware_id 불일치 이미지 거부 |
| Replay 방어 | Session ID, Sequence Number, Freshness Counter |
| Uptane-lite | Manifest 기반 검증, ECU Inventory, Campaign 단위 결과 관리 |

### UDS over ISO-TP (CAN Classic 500 Kbps)
```
0x10 DiagnosticSessionControl → 0x27 SecurityAccess
→ 0x34 RequestDownload → 0x36 TransferData (256 B/chunk)
→ 0x37 RequestTransferExit → [검증] → 0x11 ECUReset
```

### Jenkins CI/CD (Raspberry Pi 5)
1. **변경 ECU 감지** — `git diff`로 DriveECU / SensorECU 구분
2. **크로스컴파일** — `arm-none-eabi-gcc`, 슬롯별 링커스크립트
3. **정적 분석** — `cppcheck` error 등급 이상 시 중단
4. **바이너리 크기 검사** — App Slot 한계(128 KB) 초과 시 중단
5. **서명** — ECDSA 개인키는 Jenkins Credentials로만 관리
6. **OTA 배포** — UDS/ISO-TP over CAN 자동 전송, 슬롯 전환 heartbeat 확인

### Uptane 지연 활성화 (Deferred Activation)
OTA 다운로드는 주행 중에도 가능하며, 펌웨어 활성화(재부팅)는 ECU가 **DRIVE_IDLE** 상태에 진입할 때 자동으로 수행됩니다.  
Flash Erase 구간(~4초)에만 모터가 정지되고, TransferData 구간에서는 주행이 유지됩니다.

---

## App 버전 (DriveECU)

| 버전 | 동작 |
|---|---|
| v1 | 버튼 트리거 직진 주행, 10 cm 이내 장애물 감지 시 즉시 정지 |
| v2 | 10–30 cm 구간 비례 감속, 10 cm 이하 정지 |
| v3 | v2 로직 + 정지 후 자동 후진 복귀 (300 ms 대기 → 600 ms 후진) |

---

## Flash 메모리 맵 (STM32F446RE)

```
0x08000000  Bootloader  (Sector 0–4, WRP 보호)
0x08008000  Boot Metadata
0x08010000  Slot A App  (~192 KB)
0x08040000  Slot B App  (~256 KB)
```

---

## CAN ID 요약

| CAN ID | 방향 | 용도 |
|---|---|---|
| 0x7E0 / 0x7E8 | Gateway ↔ DriveECU | UDS 진단 요청 / 응답 |
| 0x7E1 / 0x7E9 | Gateway ↔ SensorECU | UDS 진단 요청 / 응답 |
| 0x100 | DriveECU → Bus | 상태 보고 (버전, 슬롯, 주행 상태) |
| 0x200 | SensorECU → Bus | 장애물 감지 + 거리값 |
| 0x201 | SensorECU → Bus | Heartbeat (100 ms 주기) |

---

## 디렉터리 구조

```
├── Bootloader/          STM32 Custom Secure Bootloader
├── DriveECU/            Drive ECU 펌웨어 (App v1/v2/v3, Slot A/B 링커)
├── SensorECU/           Sensor/Body ECU 펌웨어
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

## 하드웨어

| 부품 | 역할 |
|---|---|
| Raspberry Pi 5 | OTA Gateway, Jenkins CI/CD 서버 |
| STM32F446RE × 2 | Drive ECU, Sensor/Body ECU |
| SN65HVD230 | CAN Transceiver |
| CANable (USB-CAN) | PC/RPi CAN 연결 |
| TB6612FNG | DC 모터 드라이버 |
| HC-SR04 | 초음파 거리 센서 |
| 2WD RC 차체 | OTA 적용 결과 실증 플랫폼 |

---

## 문서

- [SRS-001](docs/SRS-001_CAN_Secure_OTA_Pipeline_v1.4.md) — 소프트웨어 요구사항 명세서
- [ADR-001](docs/ADR-001_OTA_Activation_Architecture.md) — OTA 활성화 아키텍처 의사결정
- [TEST_SPEC](docs/TEST_SPEC_OTA_v1.0.md) — 소프트웨어 테스트 명세서
- [diagram](docs/diagram.md) — 시스템 다이어그램 (Context / Block / State / Sequence)
