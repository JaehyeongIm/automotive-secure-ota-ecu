# SIT-001: 시스템 통합 테스트(On-Target 벤치) 플랜 — Secure OTA 보안·안전 기능

| 항목 | 내용 |
|---|---|
| 문서 ID | SIT-001 |
| 레벨 | 시스템/통합 테스트 — **on-target 벤치**(실 타깃 보드·실 CAN·실 액추에이터) (ASPICE SWE.6, ISO 26262-6) |
| 대상 | DriveECU + SensorECU + RPi5 게이트웨이 + CAN 버스 (on-target) |
| 작성일 | 2026-06-04 |
| 추적 | FR-BL-008, FR-AB-003/004/007, FR-AB-008, SR-FW-003, ISO 26262 안전상태 |

> 호스트 단위테스트(80개)는 *순수 로직*("입력 X→반환 Y")을 증명한다. 이 플랜은 *실보드에서
> 실제로 그렇게 동작하는가*(플래시 쓰기·부팅·점프·CAN·타이밍)를 검증한다. 둘은 상호보완이며,
> 단위테스트가 못 잡는 통합/하드웨어 결함을 여기서 잡는다.
>
> **용어 — 본 검증은 on-target 시스템 통합 테스트(SIT)다.** 실 ECU 2개 + *실물* 환경(실제 CAN·
> 모터·센서·RPi5 게이트웨이)으로 수행한다. plant(차량 동역학·센서·타 ECU)를 **실시간 시뮬레이터로
> 대체하는 HIL**(dSPACE/NI/Vector 등)과는 구분된다 — HIL은 정의상 plant가 *시뮬레이션*이지만,
> 본 벤치는 plant가 전부 *실물*이라 HIL이 아니다. 그래서 HIL이라는 명칭 대신 ASPICE SWE.5의
> 정식 용어인 **시스템 통합 테스트(System Integration Test)**로 명명한다.

---

## 1. 테스트 환경

### 1.1 장비
| 구성 | 용도 |
|---|---|
| STM32F446RE ×2 (DriveECU, SensorECU) | 피시험 ECU(SUT) |
| ST-Link v2/v3 | 펌웨어 플래시 + **메타데이터 위조 주입**(fail-closed 자극) |
| CAN 트랜시버 + 버스, RPi5 게이트웨이 | OTA 전송(UDS/ISO-TP) + 버스 관측 |
| ECU 디버그 UART ×2 (115200 8N1) | `[BL]`/`[DRIVE]`/`[SensorECU]` 로그 관측 |
| 벤치 전원, Sensor 전원/CAN 분리 스위치 | staleness 자극 |

### 1.2 관측 채널(Observable) — "테스트 포인트"
| 신호 | 출처 | 무엇을 알 수 있나 |
|---|---|---|
| **CAN 0x100** data[0]=ver, data[1]=slot | Drive heartbeat | *실제 부팅된 Drive 펌웨어의 버전·활성 슬롯* |
| **CAN 0x201** data[0]=ver, data[1]=slot | Sensor heartbeat | 실제 부팅된 Sensor 버전·슬롯 |
| **CAN 0x200** | Sensor 장애물 프레임 | Sensor 생존/침묵 (staleness 자극원) |
| **UART `[BL] …`** | 부트로더 | 검증 결정·전이·거부 사유 |
| **UART `[DRIVE]`/`[SensorECU]`** | 앱 | 앱 진입·상태·fail-safe |
| LD2 토글 | 양 ECU | heartbeat 생존(LED) |

### 1.3 메모리 맵(자극 시 참조)
```
0x08000000 부트로더(sector 0–1)   0x08008000 Meta A   0x0800C000 Meta B
0x08010000 Slot A(이미지 base; 앱 벡터 +0x200)   0x08040000 Slot B
```

### 1.4 핵심 관측 로그(정확한 문자열)
```
[BL] ECDSA OK
[BL] version vN OK
[BL] anti-rollback: vN below baseline — refusing
[BL] rollback to slot N (CONFIRMED) + reset
[BL] metadata size invalid (0x........) — fail-closed, refusing
[BL] trial start: slot N (attempt 1)   [BL] trial retry: slot N (attempt M)
[BL] 3-strike: slot N reached limit -> INVALID, rollback
[BL] fallback: candidate invalid -> slot N (CONFIRMED)   [BL] confirmed boot: slot N
[BL] Safe State: all slots invalid. Reflash via ST-Link.
[BL] Jump to 0x........  SP=...  PC=...
[SensorECU vN] Start, Slot=N        [DRIVE vN] 출발
[DRIVE] 센서 stale → fail-safe 정지
```

---

## 2. 실행 자동화 (`tools/hil_runner.py`)

> **실행 절차(사전준비·TC별 명령·복구·트러블슈팅)는 [SIT-001 RUNBOOK](SIT-001_RUNBOOK.md) 참조.**

자극→관측→판정을 자동화한다. 관측은 CAN heartbeat(0x100/0x201) + ECU UART 로그,
자극은 `ota_client.py`(OTA·`--declared-size`로 endless-data)·`forge_meta.py`+st-flash(메타 위조)·
`forge_image.py`(이미지 변조/미서명)·`cangen`(CAN flood, 반자동)·운영자 프롬프트(전원·센서·버튼)를 엮는다.

```bash
# 보드 펌웨어 적용(USB/ST-Link, OTA 불필요) — 서명 앱+CONFIRMED 메타. 보드당 1회
tools/flash.sh drive  <st-link-serial> --build      # st-info --probe 로 시리얼 확인
tools/flash.sh sensor <st-link-serial> --build

# 하드웨어 없이 파싱 로직(heartbeat 디코드·로그 매처) 검증
python tools/hil_runner.py --selftest

# 실보드 전체 실행
python tools/hil_runner.py --all --can slcan0 \
    --drive-uart /dev/tty.drive --sensor-uart /dev/tty.sensor \
    --key keys/ota_priv.pem --img fixtures/
# 단일 TC: --tc 01..08  (05 endless-data · 06 변조 · 07 미서명 · 08 CAN flood[반자동])
```

픽스처(전제): F1 `sign_firmware.py … --version N`, F2 `-DHIL_SELFTEST_FAIL` 빌드,
F3 `forge_meta.py --preset size0-attack`, **F4 `forge_image.py --tamper|--unsign`**(TC-06/07),
**F5 `ota_client … --declared-size 256`**(TC-05). TC-08은 `cangen`을 Pi에서 띄우라는 프롬프트(반자동).
TC별 합격기준은 §3, 판정은 로그·heartbeat 자동 대조.

---

## 3. 테스트 케이스

### SIT-TC-00 — 베이스라인: 정상 OTA + self-test commit (sanity)
- **목적/추적:** FR-AB-004 — 정상 업데이트가 부팅·자가확정(TRIAL→CONFIRMED)되는지(이후 TC의 기준)
- **전제:** Slot A=CONFIRMED v1 실행 중(heartbeat 0x100: ver=1, slot=0)
- **절차:** ① 게이트웨이로 v2 서명이미지 OTA 푸시 → ② ECU 자동 리셋
- **기대(관측):**
  - 부트로더: `meta transition`(UPDATED→TRIAL) → `ECDSA OK` → `version v2 OK` → `Jump 0x08010200`(또는 B)
  - 앱: `[… v2] Start/출발`, **첫 heartbeat 후 self-confirm**
  - 두 번째 리셋(전원 재인가) 후에도 v2가 CONFIRMED로 유지(롤백 안 됨)
- **합격:** heartbeat 0x100 ver=2 + 재부팅해도 v2 유지(= self-test commit 성공)

### SIT-TC-01 — anti-rollback: 서명된 옛 버전 거부 (FR-BL-008/FR-AB-008)
- **목적:** 정상 서명됐지만 *더 낮은* 버전을 부트로더가 거부
- **전제:** Slot=CONFIRMED **v2** 실행(기준선=2). 픽스처 F1의 서명 **v1** 준비
- **절차:** ① v1 서명이미지를 비활성 슬롯으로 OTA 푸시 → ② 리셋
- **기대(관측):**
  - 부트로더: `ECDSA OK`(v1 서명은 진짜) → **`anti-rollback: v1 below baseline — refusing`**
    → `rollback to slot N (CONFIRMED) + reset` → 리셋 후 CONFIRMED v2 부팅
  - heartbeat: **v1이 한 번도 안 나타남**, v2 자동 복귀
- **합격:** v1 미부팅 + 거부 로그 + 자동으로 v2(CONFIRMED) 복귀(halt 아님). 폴백 CONFIRMED가 없으면 safe_state
- **음성 대조 SIT-TC-01b:** 같은 슬롯에 v3(상위) 푸시 → `version v3 OK` → 부팅. (상위는 허용)

### SIT-TC-02 — 3-strike 롤백: 고장 업데이트 자동 복구 (FR-AB-007)
- **목적:** self-test 실패 업데이트가 3회 시도 후 이전 CONFIRMED로 롤백
- **전제:** Slot A=CONFIRMED **v2**(정상). 픽스처 F2 **고장 v3**(서명 OK, self-test 실패)
- **절차:** ① 고장 v3를 Slot B로 OTA 푸시(→UPDATED, active=B) → ② 자동 리셋, 이후 개입 없이 관측
- **기대(관측 — 부팅 사이클):**
  | 부팅 | 로그 | 결과 |
  |---|---|---|
  | 1 | `trial start: slot 1 (attempt 1)` → Jump B | 앱 hang → IWDG 리셋(~8s) |
  | 2 | `trial retry: slot 1 (attempt 2)` → Jump B | hang → 리셋 |
  | 3 | `trial retry: slot 1 (attempt 3)` → Jump B | hang → 리셋 |
  | 4 | `3-strike: slot 1 reached limit -> INVALID, rollback` → Slot A | v2 부팅 |
- **기대 최종:** heartbeat 0x100 **ver=2, slot=0**(롤백 완료), 고장 v3 더 이상 시도 안 됨
- **합격:** ≤4 사이클 내 v2/SlotA 자동 복구 + 각 단계 trial/3-strike 로그 관측

### SIT-TC-03 — fail-closed: size=0 서명검증 우회 차단 (FR-AB-003)
- **목적:** 메타 size가 비정상이면 ECDSA를 *건너뛰지 않고* 부팅 거부
- **전제:** Slot A=CONFIRMED v2 실행
- **절차:** ① ST-Link로 픽스처 F3(slot_a_size=0, CRC 유효, seq↑) 메타를 0x08008000 주입 → ② 리셋
- **기대(관측):** 부트로더 **`metadata size invalid (0x00000000) — fail-closed, refusing`** → `Safe State`. **앱 미부팅(heartbeat 없음)**
- **합격:** size=0인데도 부팅되지 않음 + fail-closed 로그
- **음성 대조 SIT-TC-03b:** 정상 size 메타로 복구(재-OTA 또는 정상 메타 주입) → `ECDSA OK` → 정상 부팅(정상 경로 회귀 확인)

### SIT-TC-04 — 센서 staleness fail-safe (ISO 26262 안전상태)
- **목적:** Sensor 침묵 시 Drive가 타임아웃 내 정지
- **전제:** 양 ECU 정상, Sensor 0x200 송신 중(장애물 없음), Drive 출발(B1) → 전진 중(모터 ON, 0x100 driving=1)
- **절차:** ① Drive 주행 확인 → ② **SensorECU 전원/CAN 분리**(0x200 중단)
- **기대(관측):** 마지막 0x200 후 **~150ms 내** Drive `[DRIVE] 센서 stale → fail-safe 정지`, 모터 정지(0x100 driving=0)
- **합격:** 센서 침묵 후 ≤~150ms 내 정지 + stale 로그
- **음성 대조 SIT-TC-04b(시동 stale):** Sensor OFF 상태로 Drive 전원 인가 → B1 눌러도 **출발 안 함**(첫 0x200 전 = stale). Sensor ON 후 B1 → 정상 출발

### SIT-TC-05 — endless-data: 누적 수신 상한·완료 검증 (FR-CAN-012/013, SR-ATK-007)
- **목적/추적:** 작은 image_size 선언 후 초과 전송 시 거부 + 세션 종료(FR-CAN-012), 불완전 전송 거부(FR-CAN-013). TC-ATK-007 ↔ [ISS-SEC-001](troubleshooting/ISS-SEC-001_endless-data-no-cumulative-cap.md)
- **전제:** Unlock 후 DOWNLOADING 진입(0x34 size=N, 예 256B)
- **절차:** ① RequestDownload size=N → ② TransferData(0x36)로 누적이 N을 **초과**하도록 전송
- **기대(관측):** 초과 블록에서 `[UDS] NRC SID=0x36 code=0x31` → **세션 종료**(이후 0x36은 `code=0x22`). 슬롯 경계 밖 미기록
- **합격:** 초과 시점 0x31 + 추가 전송 거부(0x22). 정상 size 전송은 그대로 완료(회귀 없음)
- **음성 대조 SIT-TC-05b(불완전):** size=N 선언 후 N 미만만 전송하고 0x37 → `NRC … code=0x24`
- **자극/실행:** `hil_runner.py --tc 05` (내부: `ota_client … --declared-size 256`로 작은 size 선언+초과 전송). 단위는 `test_uds_state` 2종으로 이미 PASS

### SIT-TC-06 — firmware 변조 거부 (TC-ATK-001)
- **목적/추적:** 서명영역 1바이트 변조 → ECDSA 검증 실패 → 부팅 거부. TC-ATK-001, ECDSA 게이트(FR-AB-003 REQUIRED)
- **전제:** 정상 서명 v2 이미지 보유
- **절차:** ① `tools/forge_image.py <signed.bin> --tamper` (코드 1바이트 flip, 64B 서명은 유지) → ② OTA 푸시 → ③ 리셋
- **기대(관측):** `[BL] ECDSA FAILED at 0x........ — refusing to boot` → 후보 미부팅(이전 CONFIRMED 유지/또는 safe_state)
- **합격:** 변조 이미지 미부팅 + `ECDSA FAILED` 로그
- **음성 대조:** 원본(미변조) 서명 이미지는 `[BL] ECDSA OK` → 정상 부팅(정상 경로 회귀)

### SIT-TC-07 — unsigned/서명무효 거부 (TC-ATK-002)
- **목적/추적:** 서명 64B를 0으로 만든 이미지 → size 정상이라 REQUIRED 경로 진입, ECDSA 실패로 거부(우회 불가). TC-ATK-002, FR-AB-003
- **전제:** 정상 size(REQUIRED 경로)
- **절차:** ① `tools/forge_image.py <signed.bin> --unsign` (마지막 64B = 0x00) → ② OTA 푸시 → ③ 리셋
- **기대(관측):** `[BL] ECDSA FAILED … refusing`(size 정상이므로 SKIP/fail-open 아님)
- **합격:** 미서명 이미지 미부팅 + `ECDSA FAILED` 로그

### SIT-TC-08 — CAN flood 중 OTA: 부분 이미지 미활성·brick 없음 (TC-ATK-008)
- **목적/추적:** 버스 폭주 중 OTA가 깨져도 부분 이미지로 부팅하지 않고 이전 CONFIRMED를 유지. TC-ATK-008, SR-ATK-008
- **전제:** OTA 진행 가능 상태(Unlock)
- **절차(반자동):** `hil_runner.py --tc 08` → ① "Pi에서 cangen 시작" 프롬프트(`cangen can0 -g 1 -L 8 -D r`) → ② OTA 시도 → ③ "cangen 중지 후 리셋" 프롬프트 → 관측
- **기대(관측):** 전송 깨지면 transfer_exit 미도달 → **메타 미commit** → 리셋 후 부트로더가 이전 CONFIRMED(v2) 부팅(`version v2 OK`). brick/hang 없음
- **합격:** 폭주 후 이전 펌웨어(v2) 정상 부팅 + brick 없음
- **⚠️ 한계(중요):** SRS **FR-CAN-019 "S3 세션 타임아웃(5s 무요청→Default 복귀)"는 현재 코드 미구현** — 폭주로 멈춘 세션이 *자동 abort*되지 않고 DOWNLOADING에 머문다(리셋 전까지 새 OTA 거부). 본 TC는 "부분 이미지 미활성·brick 없음"만 판정하고, **S3 자동복귀는 별도 미구현 항목(§19.1 후속)**으로 분리한다.
- **비고:** 부하 시험(`cangen`=can-utils). 정량 KPI(주입 부하율 vs 복구) 기록 권장

---

## 4. 추적 매트릭스
| TC | 요구사항 | 단위테스트(로직) | on-target(실보드) |
|---|---|---|---|
| TC-00 | FR-AB-004 | test_ota_meta(self_confirm) | 본 플랜 |
| TC-01 | FR-BL-008/FR-AB-008 | test_anti_rollback(version_allowed) | 본 플랜 |
| TC-02 | FR-AB-007 | test_meta_lifecycle(plan_boot 3-strike) | 본 플랜 |
| TC-03 | FR-AB-003 | test_bootloader_slot(verify_decision) | 본 플랜 |
| TC-04 | ISO 26262 안전상태 | gcc(drive_sensor_fresh) | 본 플랜 |
| TC-05 | FR-CAN-012/013 | test_uds_state(누적초과·불완전 2종) | 본 플랜 |
| TC-06 | TC-ATK-001(ECDSA 변조) | test_bootloader_slot(verify_decision REQUIRED) | 본 플랜 · forge_image --tamper |
| TC-07 | TC-ATK-002(서명무효) | test_bootloader_slot(REQUIRED) | 본 플랜 · forge_image --unsign |
| TC-08 | TC-ATK-008(no-brick) · ⚠FR-CAN-019(S3) 미구현 | — | 본 플랜 · cangen 부하(반자동) |

---

## 5. 관측성 개선 (플랜 작성 중 발견 → 반영 완료)
1. ✅ **boot event 로그** — `plan_boot`가 `OTA_BootEvent_t`(TRIAL_START/RETRY/ROLLBACK/FALLBACK/CONFIRMED/FACTORY/SAFE)를 반환하고 부트로더가 사유별 로그 출력. 3-strike는 `3-strike: slot X reached limit -> INVALID, rollback`로 명확히 관측됨(단위테스트 event 검증).
2. ✅ **anti-rollback graceful fallback** — 거부 시 halt 대신 거부 슬롯 INVALID 마킹 + 이전 CONFIRMED로 active 전환 + 리셋 → CONFIRMED 부팅. 보안(옛 버전 미실행)은 유지하면서 가용성↑. 폴백 CONFIRMED 없으면 safe_state.
3. ⬜ **fail-closed 자극엔 메타 위조 도구 필요** — `tools/forge_meta.py`(CRC 포함) 제작 전제(픽스처 F3, §2 항목 2).

---

## 6. 실행 기록

환경: ST-Link·UART = Mac(`/dev/tty.usbmodem21203`), CAN = RPi5(`can0`/socketcan),
OTA = scp+ssh로 `ota_client.py`(운영 경로 재사용), 오케스트레이션 = `tools/hil_runner.py`.

| TC | 일시 | 펌웨어/슬롯 | 관측 로그 | 결과 | 비고 |
|---|---|---|---|---|---|
| TC-01 anti-rollback | 2026-06-04 | drive A→B | `anti-rollback: v1 below baseline — refusing` → `rollback to slot 0` → `version v2 OK` | **PASS** | 실 OTA 35400B(ISO-TP/UDS), graceful fallback 확인 |
| TC-02 3-strike | 2026-06-04 | drive B(고장 v3) | `trial start` → `trial retry`×2 → `3-strike … INVALID, rollback` → `version v2 OK` | **PASS** | IWDG 3사이클(~30s). RX printf 제거(커밋 f605919) 후 |
| TC-03 fail-closed | 2026-06-04 | drive A | `metadata size invalid (0x00000000) — fail-closed, refusing` | **PASS** | `forge_meta --preset size0-attack` 주입 |
| TC-04 staleness | 2026-06-04 | drive A | `[DRIVE] 센서 stale → fail-safe 정지` | **PASS** | 주행 중 센서 0x200 분리 → ~150ms 내 정지 |
| TC-00 / TC-01b / TC-03b / TC-04b | — | | | (미실행) | 음성 대조·베이스라인은 후속 |

**판정:** 핵심 4종 **실보드 PASS(2026-06-04)** — 보안 3(fail-closed·anti-rollback·3-strike) + 안전 1(센서 staleness) 실증 완료. 음성 대조(b)는 후속. FAIL 시 8D 트러블슈팅(ISS) 발행.
