# Automotive Secure OTA ECU

CAN 버스 기반 Dual-ECU 환경에서 **UDS over ISO-TP Secure OTA 파이프라인**을 구현한 임베디드 시스템 프로젝트.
STM32F446RE 2대를 대상 ECU로, Raspberry Pi 5를 OTA Gateway 겸 Jenkins CI/CD 서버로 구성해 전장 OTA의 핵심 기능을 실물 RC 차량으로 실증합니다.

**🎬 데모** — RC 차량 OTA + A/B 슬롯 전환 실증 → **[▶ YouTube](https://youtu.be/_RbVwU_nPHI)**

[![Secure OTA 데모](https://img.youtube.com/vi/_RbVwU_nPHI/hqdefault.jpg)](https://youtu.be/_RbVwU_nPHI)

---

## 핵심 성과

- **보안 기능 6종** — Secure Boot(ECDSA-P256) · Anti-rollback · Fail-closed 검증 게이팅 · SecurityAccess(HMAC) · 3-strike 롤백 · 메타 이중화 원자성
- **안전 기능 1종** — 센서 staleness fail-safe (ISO 26262 안전상태 전이)
- **단위 테스트 83개** — 라인 ~91% · 분기 ~79% 커버리지 (`ceedling gcov:all`, strict)
- **On-target 8/8 PASS** — 실 OTA·실 CAN·실 RC차에서 정상 4 + 공격 4종(변조·미서명·endless-data·flood) 실증 → [SIT-001](docs/test/SIT-001_System_Integration_Test_Plan.md)
- **호스트 퍼징** — libFuzzer + ASAN/UBSAN으로 신규 결함 2건 발굴·차단(sha256 UB · UDS 스택 오버플로 CWE-787) → [FT-001](docs/test/FT-001_Fuzz_Test_Plan_Report.md)
- **규모** — ~6.2K LOC C(부트로더 + 2앱) + ~2K Python · 130+ commits

> 요구사항(SRS·HARA·TARA) → 설계(SDD·ADR) → TDD 구현 → 단위·on-target 검증까지 V-모델 추적성을 산출물로 닫았습니다.

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

## 주요 구현

### Secure Bootloader

- ECDSA-P256 서명 검증 후 App jump (uECC 라이브러리 직접 포팅)
- A/B 슬롯 — 비활성 슬롯에만 기록, 검증 실패 시 반대 슬롯 자동 Fallback
- 부팅 시도 3회 초과 시 Rollback, Boot Metadata CRC 검증, IWDG Watchdog
- Bootloader 영역 STM32 WRP(Write Protection) 하드웨어 잠금

### OTA 보안

구현 상태를 정직하게 표기합니다 — ✅ 구현+on-target 검증, ⬜ 계획/로드맵.

| 항목 | 상태 | 내용 |
|---|---|---|
| 무결성 | ✅ on-target | SHA-256 이미지 해시 |
| 인증성 | ✅ on-target | ECDSA-P256 서명 검증 (uECC 직접 포팅) |
| Anti-rollback | ✅ on-target | 이미지 헤더 `fw_version` vs CONFIRMED 슬롯 기준선, 다운그레이드 거부 |
| Fail-closed 검증 | ✅ on-target | 메타 `size` 비정상 시 검증 우회 차단 (deny-by-default, CWE-636) |
| 3-strike 롤백 | ✅ on-target | 시험부팅 3회 초과 시 INVALID + 이전 CONFIRMED 자동 롤백 |
| SecurityAccess | ✅ on-target | Key = HMAC-SHA256(PSK, Seed)[:4], 3회 실패 → 10초 잠금 |
| 메타 이중화 원자성 | ✅ 단위 | 섹터 2·3 redundant + CRC32 + seq, ping-pong 원자적 갱신(전원차단 안전) |
| ECU 식별 | ✅ 단위 | 서명 헤더 `target_ecu_id` ≠ 자기 ID면 OTA 거부 (NRC 0x31) |
| Replay 방어 | ⬜ 계획 | Session / Sequence / Freshness |
| Uptane-lite | ⬜ 계획 | Manifest 검증·ECU Inventory·Campaign |

> SecurityAccess seed(SW nonce)·잠금(RAM)·anti-rollback 기준선(CRC 메타)은 F446 TRNG/Secure NV 부재에 따른 한계가 있으며, 정석은 Secure Element([ADR-006](docs/design/adr/ADR-006_Secure_Element_Adoption.md))로 해소합니다. 전 항목의 잔여 위험과 후속 해소 트리거는 [SRS §19.1 한계 및 잔여 위험 레지스터](docs/requirements/SRS-001_CAN_Secure_OTA_Pipeline.md)에 정리했습니다.

### UDS over ISO-TP (CAN Classic 500 Kbps)

ISO-TP SF/FF/CF/FC 프레임 처리와 UDS 상태머신을 C로 직접 구현.

```
0x10 DiagnosticSessionControl → 0x27 SecurityAccess
→ 0x34 RequestDownload → 0x36 TransferData (256 B/chunk)
→ 0x37 RequestTransferExit → ECUReset
```

### 주행 중 OTA 차단 (Uptane 지연 활성화)

Gateway가 CAN heartbeat의 `driving_state`를 모니터링해 ECU가 IDLE일 때만 OTA를 시작합니다. Flash Erase 중 CPU 블로킹으로 인한 주행 중 위험을 원천 차단하며, Uptane의 "안전 조건 확인 후 설치" 원칙을 구현합니다.

### CI/CD — CI와 배포 분리 + 승인 게이트

`git push`는 **CI(검증)만** 수행하고, 차량 배포는 태그 릴리스(`vN`) + **명시적 배포 승인 게이트**(승인자 기록) 후에만 나갑니다. ([ADR-008](docs/design/adr/ADR-008_OTA_Trigger_CI_Deploy_Separation.md) — UN R156 SUMS 승인 / Uptane Image·Director 분리로 blast-radius 최소화)

- **CI (모든 push)** — 단위 테스트(`ceedling test:all`) → 정적 분석(`cppcheck`) → 변경 ECU 컴파일 검증
- **릴리스·배포 (태그 `vN`)** — 슬롯별 크로스컴파일 + ECDSA 서명(`version=N`, 개인키는 Jenkins Credentials) → 배포 승인 게이트 → ECU IDLE 확인 → UDS/ISO-TP 전송 → heartbeat로 슬롯 전환 검증

---

## 검증

**① 호스트 단위 테스트** (`ceedling gcov:all`) — 83개 · 라인 91% / 분기 79%(strict). HAL 의존부와 분리한 순수 코어(메타 상태머신·anti-rollback·검증 게이팅·crypto)를 양성 + **음성 테스트**(잘못된 SID·세션·시퀀스·`size` 위조·타 ECU 이미지 등 거부 경로)로 검증.

**② On-target(실보드) 검증** — [SIT-001](docs/test/SIT-001_System_Integration_Test_Plan.md) · 실 ECU 2대·실 CAN·실 OTA

| TC | 검증 | 결과 |
|---|---|---|
| TC-01 | anti-rollback — 옛 버전(v1) OTA → 거부 + v2 자동 복귀 | ✅ PASS |
| TC-02 | 3-strike — 고장 업데이트 → 3회 시도 후 자동 롤백 | ✅ PASS |
| TC-03 | fail-closed — `size=0` 위조 메타 → 서명검증 우회 차단 | ✅ PASS |
| TC-04 | 센서 staleness — 센서 침묵 → ~150ms 내 fail-safe 정지 | ✅ PASS |
| TC-05 | endless-data — 작은 size 선언 후 초과 전송 → `NRC 0x31` + 세션종료 | ✅ PASS |
| TC-06 | firmware 변조 — 1바이트 변조 → `ECDSA FAILED` 부팅거부 | ✅ PASS |
| TC-07 | 미서명 — 서명 64B=0 → `ECDSA FAILED`(우회 불가) | ✅ PASS |
| TC-08 | CAN flood — 폭주 중 OTA 깨져도 brick 없이 known-good 유지 | ✅ PASS |

> 실 RC차 on-target 벤치 테스트(실물 환경). plant를 실시간 시뮬레이션하는 엄밀한 의미의 HIL과는 구분됩니다.

---

## 레퍼런스

<details>
<summary><b>하드웨어</b></summary>

| 부품 | 역할 |
|---|---|
| Raspberry Pi 5 | OTA Gateway, Jenkins CI/CD 서버 |
| STM32F446RE × 2 | Drive ECU, Sensor/Body ECU |
| SN65HVD230 | CAN Transceiver |
| CANable (USB-CAN) | RPi ↔ CAN 버스 연결 |
| TB6612FNG | DC 모터 드라이버 |
| HC-SR04 | 초음파 거리 센서 |
| 2WD RC 차체 | OTA 적용 결과 실증 플랫폼 |

</details>

<details>
<summary><b>Flash 메모리 맵 (STM32F446RE) & CAN ID</b></summary>

```
0x08000000  Bootloader      (Sector 0–1, WRP 보호)
0x08008000  Boot Metadata A  (Sector 2)  ┐ 이중화(redundant)
0x0800C000  Boot Metadata B  (Sector 3)  ┘ CRC32 + seq_counter, 원자적 갱신
0x08010000  Slot A App       (~192 KB)  ┐ 앱 벡터 = slot+0x200
0x08040000  Slot B App       (~256 KB)  ┘ (앞 0x200 = 서명 이미지 헤더)
```

| CAN ID | 방향 | 용도 |
|---|---|---|
| 0x7E0 / 0x7E8 | Gateway ↔ DriveECU | UDS 진단 요청 / 응답 |
| 0x7E1 / 0x7E9 | Gateway ↔ SensorECU | UDS 진단 요청 / 응답 |
| 0x100 | DriveECU → Bus | 상태 보고 (버전, 슬롯, 주행 상태) |
| 0x200 | SensorECU → Bus | 장애물 감지 + 거리값 |
| 0x201 | SensorECU → Bus | Heartbeat (100 ms 주기) |

</details>

<details>
<summary><b>디렉터리 구조</b></summary>

```
├── Bootloader/           STM32 Custom Secure Bootloader
├── DriveECU/             Drive ECU 펌웨어 (App v1/v2, Slot A/B 링커)
├── SensorECU/            Sensor/Body ECU 펌웨어
├── test/unit/            Ceedling 단위 테스트 83개 (gcov 커버리지)
├── fuzz/                 libFuzzer 호스트 하니스 (FH-1·FH-3, ASAN/UBSAN)
├── tools/                ota_client·sign_firmware·forge_meta·hil_runner·can_monitor
├── ci/                   build.sh(슬롯별 크로스컴파일) · read_slot.py
├── Jenkinsfile           CI/CD (CI / 태그릴리스 / 승인 게이트 / 배포)
├── project.yml           Ceedling 설정 (gcov 포함)
└── docs/                 SRS · SDD · HARA · TARA · SIT · TR · RTM · FT · ADR · 트러블슈팅
```

</details>

<details>
<summary><b>빌드·테스트 실행</b></summary>

```bash
# 의존성 (Raspberry Pi)
sudo apt install -y ruby-full gcc-arm-none-eabi cppcheck
gem install ceedling && pip install python-can

# 단위 테스트 + 커버리지
ceedling test:all          # 83개 단위 테스트
ceedling gcov:all          # 라인/분기 커버리지

# On-target(실보드) 검증 — 실 ECU·CAN 연결 후 (SIT-001)
python3 tools/hil_runner.py --selftest      # 파싱 로직 검증(장비 불필요)
python3 tools/hil_runner.py --ecu drive --build --setup --tc 03 --uart <UART포트>

# Jenkins — (1) push = CI 검증만
git commit -am "feat: DriveECU app v2" && git push origin main
# (2) 태그 vN = 빌드·서명 → 배포 승인(승인자 기록) → ECU IDLE 대기 → OTA → 슬롯 전환 확인
git tag v2 && git push origin v2
```

</details>

> **알려진 문제** — 직진 주행 미보장: 매 실행마다 진행 방향이 좌·우 불규칙. 원인(모터 PWM 타이밍·기구적 요인) 분석 중.

---

## 문서

- [프로젝트 요약 (1-page)](docs/portfolio/PORTFOLIO_ONEPAGER.md) · [시스템 다이어그램](docs/design/diagram.md)
- **요구·설계** — [SRS-001](docs/requirements/SRS-001_CAN_Secure_OTA_Pipeline.md) · [SDD-001](docs/design/SDD-001_Secure_OTA_Software_Design.md) · [RTM-001](docs/requirements/RTM-001_Requirements_Traceability_Matrix.md)
- **안전·보안 분석** — [HARA-001](docs/safety/HARA-001_Hazard_Analysis_Risk_Assessment.md) (ISO 26262) · [TARA-001](docs/security/TARA-001_Threat_Analysis_Risk_Assessment.md) (ISO/SAE 21434)
- **테스트** — [SIT-001](docs/test/SIT-001_System_Integration_Test_Plan.md) · [TR-001](docs/test/TR-001_Test_Report.md) · [FT-001](docs/test/FT-001_Fuzz_Test_Plan_Report.md)
- **설계 의사결정 / 트러블슈팅** — ADR 10건 → [docs/design/adr/](docs/design/adr/) · 8D 분석 17건 → [docs/troubleshooting/](docs/troubleshooting/)
