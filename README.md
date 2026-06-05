# Automotive Secure OTA ECU

CAN 버스 기반 Dual ECU 환경에서 **UDS over ISO-TP Secure OTA 파이프라인**을 구현한 임베디드 시스템 프로젝트.  
STM32F446RE 2대를 대상 ECU로, Raspberry Pi 5를 OTA Gateway 겸 Jenkins CI/CD 서버로 구성해 전장 OTA의 핵심 기능을 실물 RC 차량으로 실증합니다.

---

## 핵심 지표 (KPI)

| | |
|---|---|
| 🔐 **보안 기능 6종** | Secure Boot(ECDSA-P256)·Anti-rollback·Fail-closed 검증게이팅·SecurityAccess(HMAC)·3-strike 롤백·메타 이중화 원자성 |
| 🛡️ **안전 기능 1종** | 센서 staleness fail-safe (ISO 26262 안전상태 전이) |
| ✅ **단위 테스트 74개** | 라인 커버리지 **92%** · 분기 커버리지 **81%** (`ceedling gcov:all`, strict) |
| 🔬 **On-target 검증 4/4 PASS** | 실 OTA·실 CAN·실 RC차로 보안 3 + 안전 1 실증 ([HIL-001](docs/HIL-001_HIL_Test_Plan.md)) |
| 📄 **표준 산출물** | SRS·HARA·TARA·SDD·HIL + ADR×8 + 트러블슈팅(8D)×10 — ASPICE SWE.1~6 추적 |
| 📦 **규모** | ~6.2K LOC C(부트로더+2앱) + ~2K Python · 125 commits |

> **프로젝트 서사.** README/SRS가 *구현됐다고 명세한* 보안기능 다수가 실제 코드엔 없던 **문서–코드 갭을 발견** →
> 실무자가 던지는 적대적 질문(신뢰경계·원자성·측정가능성·fail-closed 등)으로 **요구사항을 강화** →
> HAL 의존부와 순수 로직을 분리해 **TDD로 구현·단위검증** → 운영 OTA 경로 그대로 **on-target(실보드) 실증**.
> 요구↔설계↔코드↔테스트의 V-모델을 양방향으로 닫았습니다.

---

## 시스템 구성

```
[개발자 PC]
     │ git push (코드)  /  git tag vN (릴리스)
     ▼
[Raspberry Pi 5] ── Jenkins
     │   ├ CI (push마다)      : 단위테스트 → 정적분석 → 컴파일 검증
     │   ├ 릴리스 (태그 vN)   : 빌드 → ECDSA 서명(ver=N) → 아티팩트 보관
     │   ├ ★배포 승인 게이트  : input — 승인자 기록 (UN R156 SUMS)
     │   └ OTA Gateway       : 승인 후 ECU IDLE 감지 시 UDS/ISO-TP 전송
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

> 구현 상태를 정직하게 표기합니다 — ✅ 구현+on-target 검증, 🔶 부분, ⬜ 계획/로드맵.

| 항목 | 상태 | 내용 |
|---|---|---|
| 무결성 | ✅ HIL | SHA-256 이미지 해시 |
| 인증성 | ✅ HIL | ECDSA-P256 서명 검증 (uECC 직접 포팅) |
| Anti-rollback | ✅ HIL | 서명 이미지 헤더 `fw_version` vs CONFIRMED 슬롯 기준선, 다운그레이드 거부 |
| Fail-closed 검증 | ✅ HIL | 메타 `size` 비정상 시 *검증 우회 차단*(deny-by-default, CWE-636) |
| 3-strike 롤백 | ✅ HIL | 시험부팅 3회 초과 시 INVALID + 이전 CONFIRMED 자동 롤백 |
| SecurityAccess | ✅ HIL | Key = **HMAC-SHA256(PSK, Seed)[:4]**, 3회 실패 → 10초 잠금 |
| 메타 이중화 원자성 | ✅ 단위 | 섹터 2·3 redundant + CRC32 + seq, ping-pong 원자적 갱신(전원차단 안전) |
| ECU 식별 | 🔶 부분 | 서명 헤더에 `target_ecu_id` 포함(부트로더 강제 거부는 후속) |
| Replay 방어 | ⬜ 계획 | Session/Sequence/Freshness (FR-CAN-017) |
| Uptane-lite | ⬜ 계획 | Manifest 검증·ECU Inventory·Campaign (로드맵) |

> ⚠️ SecurityAccess의 seed는 F446 TRNG 부재로 SW nonce(엔트로피 약함, [ADR-004](docs/adr/ADR-004_SecurityAccess_Seed_RNG.md)), 잠금은 RAM([ADR-003](docs/adr/ADR-003_SecurityAccess_Lockout_Storage.md)), anti-rollback 기준선은 CRC 메타라 물리공격엔 한계 — 정석은 Secure Element([ADR-006](docs/adr/ADR-006_Secure_Element_Adoption.md))로 해소(HW 도입 중).

### UDS over ISO-TP (CAN Classic 500 Kbps)

ISO-TP SF/FF/CF/FC 프레임 처리 및 UDS 상태머신을 C로 직접 구현.

```
0x10 DiagnosticSessionControl → 0x27 SecurityAccess
→ 0x34 RequestDownload → 0x36 TransferData (256 B/chunk)
→ 0x37 RequestTransferExit → ECUReset
```

### Uptane 지연 활성화

Gateway가 CAN heartbeat의 `driving_state`를 모니터링하여 ECU가 IDLE 상태일 때만 OTA 전송을 시작합니다. Flash Erase 중 CPU 블로킹으로 인한 주행 중 위험을 원천 차단하며, Uptane 표준의 "안전 조건 확인 후 설치" 원칙을 구현합니다.

### Jenkins CI/CD 파이프라인 — CI와 배포 분리 + 승인 게이트

**`git push`는 CI(검증)만** 수행하고 **차량 배포는 분리**합니다 — 태그 릴리스(`vN`) + **명시적 배포 승인 게이트**(승인자 기록) 후에만 OTA가 나갑니다. ([ADR-008](docs/adr/ADR-008_OTA_Trigger_CI_Deploy_Separation.md) — UN R156 SUMS 승인 / Uptane *Image·Director* 분리로 blast-radius 최소화)

**CI — 모든 `git push`**
1. **단위 테스트** — `ceedling test:all`, 실패 시 이후 차단
2. **정적 분석** — `cppcheck` error 이상 시 중단
3. **컴파일 검증** — 변경 ECU(`git diff`)만, 슬롯별 링커스크립트

**릴리스·배포 — 태그 `vN`에서만**
4. **빌드 + 서명** — 슬롯별 크로스컴파일 + 크기 검사 + ECDSA 서명(`version=N`, anti-rollback). 개인키는 Jenkins Credentials로만 관리
5. **★ 배포 승인 게이트** — Jenkins `input`으로 **승인자를 기록**(UN R156 SUMS 승인의 미니어처)
6. **OTA 배포** — 승인 후 ECU IDLE 확인 → UDS/ISO-TP 전송 → heartbeat로 슬롯 전환 검증

---

## 검증 (Verification) — 2단계

**① 호스트 단위 테스트** (순수 로직, `ceedling gcov:all`)
- **74개** 테스트 · 라인 커버리지 **92%** · 분기 커버리지 **81%**(strict: taken-at-least-once)
- 양성 + **음성 테스트**(잘못된 SID·세션·시퀀스·`size=0`/초과 등 *거부 경로*)로 보안/안전의 분기 검증
- HAL 의존부와 분리한 *순수 코어*(메타 상태머신·anti-rollback·검증게이팅·crypto)를 호스트에서 검증

**② On-target(실보드) 검증** — [HIL-001](docs/HIL-001_HIL_Test_Plan.md) · 실 ECU 2대·실 CAN·실 OTA

| TC | 검증 | 결과 |
|---|---|---|
| TC-01 | anti-rollback — 옛 버전(v1) OTA → 거부 + v2 자동 복귀 | ✅ PASS |
| TC-02 | 3-strike — 고장 업데이트 → 3회 시도 후 자동 롤백 | ✅ PASS |
| TC-03 | fail-closed — `size=0` 위조 메타 → 서명검증 우회 차단 | ✅ PASS |
| TC-04 | 센서 staleness — 센서 침묵 → ~150ms 내 fail-safe 정지 | ✅ PASS |

> 실 RC차 **on-target 벤치** 테스트(실물 환경). plant를 실시간 시뮬레이션하는 *엄밀한 의미의 HIL*과는 구분됩니다.

---

## Flash 메모리 맵 (STM32F446RE)

```
0x08000000  Bootloader      (Sector 0–1, WRP 보호)
0x08008000  Boot Metadata A  (Sector 2)  ┐ 이중화(redundant)
0x0800C000  Boot Metadata B  (Sector 3)  ┘ CRC32 + seq_counter, 원자적 갱신
0x08010000  Slot A App       (~192 KB)
0x08040000  Slot B App       (~256 KB)
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
    ├── SRS-001_CAN_Secure_OTA_Pipeline.md
    ├── adr/
    │   ├── ADR-001_OTA_Activation_Architecture.md
    │   ├── ADR-002_Boot_Timing_Measurement.md
    │   ├── ADR-003_SecurityAccess_Lockout_Storage.md
    │   ├── ADR-004_SecurityAccess_Seed_RNG.md
    │   ├── ADR-005_Troubleshooting_Doc_Naming.md
    │   ├── ADR-006_Secure_Element_Adoption.md
    │   ├── ADR-007_Anti_Rollback_Design.md
    │   └── ADR-008_OTA_Trigger_CI_Deploy_Separation.md
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

# 단위 테스트 + 커버리지
ceedling test:all          # 74개 단위 테스트
ceedling gcov:all          # 라인/분기 커버리지 측정

# On-target(실보드) 검증 — 실 ECU·CAN 연결 후 (HIL-001)
python3 tools/hil_runner.py --selftest      # 파싱 로직 검증(장비 불필요)
python3 tools/hil_runner.py --ecu drive --build --setup --tc 03 --uart <UART포트>

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

- [SRS-001](docs/SRS-001_CAN_Secure_OTA_Pipeline.md) — 소프트웨어 요구사항 명세서
- [SDD-001](docs/SDD-001_Secure_OTA_Software_Design.md) — 소프트웨어 설계서 (SWE.2 아키텍처 + SWE.3 상세설계, 추적성)
- [HARA-001](docs/HARA-001_Hazard_Analysis_Risk_Assessment.md) — 위험원 분석·리스크 평가 (ISO 26262)
- [TARA-001](docs/TARA-001_Threat_Analysis_Risk_Assessment.md) — 위협 분석·리스크 평가 (ISO/SAE 21434)
- [HIL-001](docs/HIL-001_HIL_Test_Plan.md) — on-target(실보드) 테스트 플랜·실행기록 (보안 3 + 안전 1 PASS)
- [ADR-001](docs/adr/ADR-001_OTA_Activation_Architecture.md) — OTA 활성화 아키텍처 의사결정
- [ADR-002](docs/adr/ADR-002_Boot_Timing_Measurement.md) — 부트 타이밍(Tboot) 측정 방식 결정
- [ADR-003](docs/adr/ADR-003_SecurityAccess_Lockout_Storage.md) — SecurityAccess 잠금 상태 저장 위치(RAM vs NV)
- [ADR-004](docs/adr/ADR-004_SecurityAccess_Seed_RNG.md) — SecurityAccess Seed 난수 생성(TRNG 부재·SP 800-90 미준수)
- [ADR-005](docs/adr/ADR-005_Troubleshooting_Doc_Naming.md) — 트러블슈팅 문서 파일명·ID 규칙
- [ADR-006](docs/adr/ADR-006_Secure_Element_Adoption.md) — Secure Element(ATECC608A) 도입 — TRNG·NV 한계 하드웨어 해소
- [ADR-007](docs/adr/ADR-007_Anti_Rollback_Design.md) — Anti-rollback 설계(서명 이미지 헤더 + 메타 버전 기준선)
- [ADR-008](docs/adr/ADR-008_OTA_Trigger_CI_Deploy_Separation.md) — OTA 트리거: CI 빌드와 차량 배포 분리(태그 릴리스 + 승인 게이트)
- [TEST_SPEC](docs/TEST_SPEC_OTA_v1.0.md) — 소프트웨어 테스트 명세서
- [diagram](docs/diagram.md) — 시스템 다이어그램 (Context / Block / State / Sequence)
