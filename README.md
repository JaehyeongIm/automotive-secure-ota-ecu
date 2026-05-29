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

## 테스트 가이드

### 사전 준비

```bash
# Ruby + Ceedling (C 단위 테스트)
sudo apt install -y ruby-full
gem install ceedling
export PATH="$(ruby -e 'puts Gem.user_dir')/bin:$PATH"   # ~/.bashrc 에도 추가

# Python 의존성 (CAN / OTA 통합 테스트)
pip install python-can

# arm 크로스컴파일러 (자동 빌드 시)
sudo apt install -y gcc-arm-none-eabi   # Raspberry Pi / Ubuntu
# macOS: brew install --cask gcc-arm-embedded
```

---

### 전체 테스트 — 한 번에 실행

```bash
# 자동 빌드+서명 포함 (권장)
python3 ci/test_all.py \
    --channel can0 \
    --key <개인키 파일 이름>

# 이미 빌드된 펌웨어 파일 직접 지정
python3 ci/test_all.py \
    --channel can0 \
    --fw-drive-a  artifacts/drive_slotA_signed.bin \
    --fw-drive-b  artifacts/drive_slotB_signed.bin \
    --fw-sensor-a artifacts/sensor_slotA_signed.bin \
    --fw-sensor-b artifacts/sensor_slotB_signed.bin
```

실행 순서:
1. **Phase 1** — `ceedling test:all` (C 단위 테스트)
2. **Phase 2** — 3라운드 × (DriveECU OTA → SensorECU OTA)

Phase 1이 실패하면 Phase 2를 진행하지 않습니다.

| 옵션 | 설명 |
|---|---|
| `--count N` | OTA 반복 횟수 (기본 3) |
| `--cf-delay N` | ISO-TP CF 간격 초 (기본 0.005) |
| `--skip-unit` | 단위 테스트 건너뜀 |
| `--skip-ota` | OTA 통합 테스트 건너뜀 |
| `--interface slcan` | USB-CAN (slcan) 사용 시 |

---

### OTA 시연 — App 버전 업데이트

DriveECU App을 v1 → v2 → v3 순서로 OTA하여 주행 동작 변화를 시연합니다.

| 버전 | 동작 |
|---|---|
| v1 | 직진 주행, 10 cm 이내 장애물 감지 시 **즉시 정지** |
| v2 | 10–30 cm 구간 비례 **감속**, 10 cm 이하 정지 |
| v3 | v2 로직 + 정지 후 **자동 후진 복귀** (300 ms 대기 → 600 ms 후진) |

```bash
# v1 → v2 → v3 순서로 전부 시연
python3 ci/demo_ota.py \
    --channel can0 \
    --key <개인키 파일> \
    --versions 1 2 3

# 특정 버전만 (예: v3만)
python3 ci/demo_ota.py --channel can0 --key <개인키 파일> --versions 3

# v2 → v3만
python3 ci/demo_ota.py --channel can0 --key <개인키 파일> --versions 2 3
```

**실행 순서:**
1. **Phase 1** — 지정 버전 펌웨어 빌드+서명 (빌드 로그 생략)
2. **Phase 2** — 초기 ECU 상태 확인
3. **Phase 3** — 지정 버전 순서로 OTA 수행 + 버전 검증

| 옵션 | 설명 |
|---|---|
| `--versions 1 2 3` | OTA할 버전 (스페이스로 구분, 순서대로 실행) |
| `--cf-delay N` | ISO-TP CF 간격(초) (기본 0.005) |
| `--interface slcan` | USB-CAN (slcan) 사용 시 |

**시연 영상 촬영 팁:**
- 빌드가 완료되고 `[Phase 3] OTA 시연` 이 출력된 시점부터 촬영을 시작하세요.
- 각 버전 OTA 완료(`결과: PASS`) 직후 차량 동작을 바로 확인할 수 있습니다.
- `단위 테스트 + OTA 시연` 을 한 화면에 모두 담으려면 `test_all.py` 와 `demo_ota.py` 를 터미널 분할 화면으로 나란히 실행하세요.

---

### git push → Jenkins OTA 시연 (핵심 시연)

`git push` 한 번으로 Jenkins가 변경된 ECU를 감지해 자동으로 OTA를 수행합니다.  
Raspberry Pi에 Jenkins가 실행 중이어야 하며, ngrok으로 GitHub webhook을 수신합니다.

---

#### 1단계 — ngrok 설치 및 실행 (Raspberry Pi)

```bash
# ngrok 설치
curl -sSL https://ngrok-agent.s3.amazonaws.com/ngrok.asc | sudo tee /etc/apt/trusted.gpg.d/ngrok.asc >/dev/null
echo "deb https://ngrok-agent.s3.amazonaws.com buster main" | sudo tee /etc/apt/sources.list.d/ngrok.list
sudo apt update && sudo apt install ngrok

# ngrok 인증 (https://dashboard.ngrok.com 에서 authtoken 발급)
ngrok config add-authtoken <YOUR_AUTHTOKEN>

# Jenkins 포트(8080) 외부 노출
ngrok http 8080
```

ngrok 실행 후 출력되는 `Forwarding` URL을 복사합니다.

```
Forwarding  https://xxxx-xxx-xxx-xxx.ngrok-free.app -> http://localhost:8080
```

---

#### 2단계 — Jenkins 설정

**Jenkins URL 등록**

Jenkins → Dashboard → Manage Jenkins → System → Jenkins URL

```
https://xxxx-xxx-xxx-xxx.ngrok-free.app
```

저장 후 Jenkins를 재시작하지 않아도 됩니다.

**GitHub Plugin 설치 확인**

Manage Jenkins → Plugins → Installed plugins 에서 `GitHub` 플러그인이 있는지 확인.  
없으면 Available plugins에서 `GitHub` 검색 후 설치.

**OTA 개인키 등록**

Manage Jenkins → Credentials → System → Global → Add Credentials

| 항목 | 값 |
|---|---|
| Kind | Secret file |
| File | `ota-private-key.pem` 업로드 |
| ID | `ota-private-key` |

---

#### 3단계 — GitHub Webhook 등록

GitHub 레포지토리 → Settings → Webhooks → Add webhook

| 항목 | 값 |
|---|---|
| Payload URL | `https://xxxx-xxx-xxx-xxx.ngrok-free.app/github-webhook/` |
| Content type | `application/json` |
| Which events | `Just the push event` |

저장 후 Recent Deliveries에서 ✅ 200 응답 확인.

---

#### 4단계 — Jenkins Job 설정

Jenkins → New Item → Pipeline (또는 기존 Job 설정)

**General 탭**
- ✅ GitHub project → Project url: `https://github.com/<user>/<repo>`

**Build Triggers 탭**
- ✅ GitHub hook trigger for GITScm polling

**Pipeline 탭**
- Definition: `Pipeline script from SCM`
- SCM: Git → Repository URL: `https://github.com/<user>/<repo>.git`
- Branch: `*/main`
- Script Path: `Jenkinsfile`

저장.

---

#### 5단계 — 시연 흐름 (개발자 PC에서)

```bash
# 1. App 버전 수정
#    DriveECU/Core/Inc/drive.h 에서 APP_VERSION 변경
#    예: #define APP_VERSION 2  →  3

# 2. 커밋 + 푸시
git add DriveECU/Core/Inc/drive.h
git commit -m "feat: DriveECU app v3 — 자동 후진 복귀 활성화"
git push origin main

# 3. Jenkins 파이프라인 자동 실행 확인
#    GitHub webhook → ngrok → Jenkins → 빌드 → 서명 → OTA → 슬롯 검증
```

Jenkins 빌드 콘솔에서 아래 순서로 진행됩니다:

```
[DriveECU] active=SlotA → target=SlotB
[BUILD] ...
[SIGN]  ...
[OTA]  TransferData → RequestTransferExit
[DriveECU] OTA 완료: SlotB 부팅 확인
```

---

#### 파이프라인 동작 조건

| 변경 파일 경로 | DriveECU OTA | SensorECU OTA |
|---|---|---|
| `DriveECU/` 하위 파일 | ✅ 실행 | — |
| `SensorECU/` 하위 파일 | — | ✅ 실행 |
| 둘 다 변경 | ✅ 실행 | ✅ 실행 (순차) |
| 그 외 (`ci/`, `docs/` 등) | — | — |

> ngrok 무료 플랜은 재시작 시 URL이 바뀝니다. 바뀔 때마다 GitHub Webhook URL을 업데이트하거나, ngrok 유료 플랜의 고정 도메인을 사용하세요.

---

## 문서

- [SRS-001](docs/SRS-001_CAN_Secure_OTA_Pipeline_v1.4.md) — 소프트웨어 요구사항 명세서
- [ADR-001](docs/ADR-001_OTA_Activation_Architecture.md) — OTA 활성화 아키텍처 의사결정
- [TEST_SPEC](docs/TEST_SPEC_OTA_v1.0.md) — 소프트웨어 테스트 명세서
- [diagram](docs/diagram.md) — 시스템 다이어그램 (Context / Block / State / Sequence)
