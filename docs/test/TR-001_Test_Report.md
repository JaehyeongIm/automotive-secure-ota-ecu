# TR-001: 소프트웨어 테스트 결과서 (Test Report)

| 항목 | 내용 |
|---|---|
| 문서 ID | TR-001 |
| 문서명 | Secure OTA ECU 시스템 소프트웨어 테스트 결과서 |
| 레벨 | SW 검증 결과 보고(ASPICE SWE.6 — Test Report) |
| 작성일 | 2026-06-05 |
| 대상 | Bootloader + DriveECU/SensorECU App + 공유 코어(ota_meta·crypto·uds) |
| 참조 | SRS-001(v2.13), TEST_SPEC_OTA v1.0(계획), SIT-001(on-target 플랜·기록), SDD-001, project.yml |
| 표준 근거 | ISO 26262-6(검증), ISO/SAE 21434(보안검증·잔여위험), ASPICE SWE.5/6 |

> 본 결과서는 *실제로 실행한* 테스트의 **결과**를 기록한다. *계획*(TEST_SPEC·SIT-001 플랜)과 구분되며,
> 미실행·이연 항목은 단정하지 않고 **SRS §19.1 한계·잔여 위험 레지스터**로 연결한다.

---

## 1. 요약 (Verdict)

| 캠페인 | 범위 | 결과 |
|---|---|---|
| ① 호스트 단위테스트(Ceedling) | HAL 분리 *순수 코어* 로직 | **83/83 PASS** · 라인 ~91%·분기 ~79% |
| ② On-target 벤치(SIT-001) | 실 ECU 2대·실 CAN·실 OTA | **8/8 PASS** (기본 4: 보안 3+안전 1 · 공격 4: SIT-TC-05~08) |
| ③ CI/CD 파이프라인 E2E 배포(Jenkins) | git-push 트리거·빌드/ECDSA 서명·승인 게이트·양 ECU 실 OTA 배포 | **PASS** (v4 릴리스 양 ECU 슬롯전환·부팅 확인 — build #86; §4.1) |
| ④ 호스트 퍼징(libFuzzer+ASAN/UBSAN) | 순수 파서(이미지헤더·UDS 0x36) | **신규 결함 2건 발굴·차단** — F-002(sha256 UB)·F-003(UDS 스택오버플로 CWE-121) → [FT-001](FT-001_Fuzz_Test_Plan_Report.md) |

**판정: 구현 범위 PASS.** 미구현·미실행(replay·campaign·SIT 음성대조 등)은 **§19.1 잔여 위험으로 명시·수용**한다(과대 PASS 주장 없음).
실행일 — 단위: 2026-06-05(본 결과서 작성 시 재실행 확인), on-target: 2026-06-04, CI/CD E2E 배포: 2026-06-07(Jenkins build #86, §4.1).

---

## 2. 테스트 환경

| 구분 | 구성 |
|---|---|
| 단위(①) | Ceedling 0.31 / Ruby 2.6, gcc 호스트(macOS). `project.yml :source` = `SensorECU/Core/Src` + `Bootloader/Core/Src`(HAL 분리 순수코어). 실행 `ceedling test:<name>` · 커버리지 `ceedling gcov:all`(strict: taken-at-least-once) |
| on-target(②) | STM32F446RE ×2(Drive/Sensor) + ST-Link + CAN 트랜시버/버스 + RPi5 게이트웨이. 자극·관측 오케스트레이션 `tools/hil_runner.py`, OTA `ota_client.py`(운영 경로 재사용), 메타 위조 `forge_meta.py`+st-flash. 상세 SIT-001 §1 |

> 단위 범위 한계: `:source`가 Sensor/Bootloader만 컴파일 → DriveECU 고유 로직(`drive.c`)은 gcc 독립검증(6케이스)로 별도 확인(SDD §8). 본 보고서 단위 수치에는 미포함.

---

## 3. ① 호스트 단위테스트 결과 — 83/83 PASS

| 테스트 파일 | 케이스 | 결과 | 검증 대상(코어) |
|---|---:|---|---|
| `test_anti_rollback` | 10 | PASS | `version_allowed`·`img_header_read`·`ecu_id_allowed`(FR-BL-008/AB-008·FR-CAN-011) |
| `test_bootloader_slot` | 15 | PASS | `verify_decision`(fail-closed REQUIRED/REFUSE/SKIP)·슬롯 선택(FR-AB-003) |
| `test_hmac` | 3 | PASS | HMAC-SHA256 RFC 4231 벡터(FR-CAN-010) |
| `test_meta` | 7 | PASS | 메타 CRC32·`ota_meta_select`(FR-AB-005) |
| `test_meta_lifecycle` | 8 | PASS | `plan_boot`(증가-먼저·3-strike)·`plan_confirm`(FR-AB-007) |
| `test_ota_meta` | 10 | PASS | `write_pending`·`self_confirm`(RAM 하니스, FR-AB-004) |
| `test_uds_state` | 30 | PASS | SecurityAccess·세션·시퀀스·**거부 분기**(음성)·endless-data 상한/완료·**per-block 상한(F-003)**·**S3 타임아웃**(FR-CAN-009~019) |
| **합계** | **83** | **PASS (0 Failures)** | |

**커버리지(`gcov:all`, strict):** 라인 **~91%** · 분기 **~79%**. 코어별 실측 — `ota_flash` 98.3%L/73.7%B · `sha256` 94.4%L/83.3%B · `uds` 86.3%L/77.5%B · `hmac_sha256` 77.3%L/75.0%B. (HAL/페리페럴·startup 파일은 단위 범위 밖 — gcov "no coverage" 정상)

---

## 4. ② On-target 벤치 결과 (SIT-001) — 8/8 PASS (기본 4 + 공격 4)

| TC | 요구사항 | 핵심 관측 로그 | 결과 | 일시 |
|---|---|---|---|---|
| SIT-TC-01 anti-rollback | FR-BL-008 / FR-AB-008 | `anti-rollback: v1 below baseline — refusing` → `rollback to slot 0` → `version v2 OK` | **PASS** | 2026-06-04 |
| SIT-TC-02 3-strike 롤백 | FR-AB-007 | `trial start` → `trial retry`×2 → `3-strike … INVALID, rollback` → `version v2 OK` | **PASS** | 2026-06-04 |
| SIT-TC-03 fail-closed | FR-AB-003 | `metadata size invalid (0x00000000) — fail-closed, refusing` → `Safe State` | **PASS** | 2026-06-04 |
| SIT-TC-04 센서 staleness | ISO 26262 안전상태 | `[DRIVE] 센서 stale → fail-safe 정지`(센서 0x200 분리 후 ~150ms) | **PASS** | 2026-06-04 |

- 실증 경로: 실 OTA(35400 B, ISO-TP/UDS)로 전달 → 부트로더 검증·전이·거부 사유를 UART/CAN heartbeat로 자동 대조(SIT-001 §6).
- **재검증(2026-06-07):** ABOM(ISS-CAN-006)·endless-data(FR-CAN-012/013) 펌웨어 변경 후 SIT-TC-01~04 on-target **4/4 재통과(회귀 없음)**. OTA 전송은 cf-delay 0.02로 수행(ISS-OTA-006 완화). 진단 중 발견·해결 이슈: ISS-CAN-006·ISS-HW-001·ISS-SEC-002.

**공격 시나리오 on-target (SIT-TC-05~08, 2026-06-07):**

| TC | 시나리오 | 핵심 관측 | 결과 |
|---|---|---|---|
| SIT-TC-05 | endless-data | 작은 size 선언 후 초과 전송 → `NRC SID=0x36 code=0x31` + 세션종료 | **PASS** |
| SIT-TC-06 | firmware 변조 | `forge_image --tamper`(1B 변조) → `ECDSA FAILED … refusing` → Safe State | **PASS** |
| SIT-TC-07 | 미서명/서명무효 | `forge_image --unsign`(서명 64B=0) → REQUIRED 경로 `ECDSA FAILED`(우회 불가) | **PASS** |
| SIT-TC-08 | CAN flood(no-brick) | cangen 폭주 중 OTA → `Receive timeout` → 메타 미commit → known-good v2(ESR=0) | **PASS** |

- **미실행:** SIT-TC-00(베이스라인)·SIT-TC-01b/03b/04b(음성 대조) → 후속(SIT-001 §6 기록과 일치).

### 4.1 ③ CI/CD 파이프라인 E2E 배포 실증 (Jenkins)

> ADR-008(배포 트리거=릴리스 태그 / CI=브랜치 푸시 분리)·ADR-007(anti-rollback 단조 버전) 운영 경로를 Jenkins 파이프라인으로 실제 실행했다. 배포 단계는 운영 OTA 경로(`ota_client.py`·`read_slot.py`)를 그대로 사용한다(SIT-001 §1 동일 벤치).

| 빌드 | 트리거 | 핵심 관측 | 결과 |
|---|---|---|---|
| #85 | git push (GitHub webhook `POST /github-webhook/ 200`) | Detect `release=true version=4 drive=true sensor=true` → 단위/정적분석/빌드·서명(ECDSA A·B 4개) → 승인(`raspberrypi`) → Deploy DriveECU PASS → **Deploy SensorECU 실패**(`[OTA] ERROR: Receive timeout`, `OTA_EXIT=1`) → **파이프라인 FAILURE → 이전 펌웨어 유지** | 트리거·실패경로 PASS / 배포 FAIL |
| #86 | Build Now (수동 재실행, 벤치 SensorECU 재플래시 후) | 동일 단계 + **양 ECU 실 OTA** — Drive `OTA 완료: SlotB 부팅 확인`, Sensor UDS `0x7E9` 정상응답→SecurityAccess Unlock→149블록 100%→RequestTransferExit→`OTA 완료: SlotB 부팅 확인` → `Finished: SUCCESS` | **PASS** |

- **#86(성공):** 빌드 → ECDSA 서명 → **승인 게이트(UN R156 SUMS형)** → 양 ECU UDS/ISO-TP OTA → A→B 슬롯 전환·재부팅 확인까지 CI/CD E2E 전 구간 실증. anti-rollback 단조 버전 v4(계획 TC-CI-003 "v1→v2"와 흐름 동일, 버전만 상이).
- **#85(실패 경로 = FR-CICD-010 실증):** 배포 대상 SensorECU가 일시적으로 UDS 무응답(0x200/0x201 송신은 살아있으나 메인루프 `uds_process()` 미동작)이어서 OTA 실패 → 파이프라인이 **FAILURE로 처리하고 ECU를 이전 펌웨어로 유지**(부분 flash·브릭 없음). 의도적 CAN 차단 주입(계획 절차)은 아니나 **실패 처리·fail-safe 동작이 직접 실증**됐다. `tools/flash.sh sensor … --build` 재플래시로 복구 후 #86 정상(상세 [ISS-OTA-007](../troubleshooting/ISS-OTA-007_sensor-deploy-timeout-bench-hang.md)).
- **재현:** Jenkins `Build Now`(또는 릴리스 `vN` 태그 푸시) → 승인 → 콘솔/`read_slot.py --wait-reboot`로 슬롯 전환 확인.

---

## 5. 보안 공격 시나리오 커버리지 (TC-ATK-001~010 ↔ SR-ATK)

> 범례: ✅ 실증(실행 증거 있음) · 🔶 부분/간접(인접 실행 테스트로 메커니즘 확인, 전용 케이스 미실행) · ⬜ 미실행/미구현(§19.1)

| TC-ATK | 시나리오 | 상태 | 근거 / 비고 |
|---|---|---|---|
| 001 | Firmware 변조(hash) | ✅ | **SIT-TC-06 on-target PASS** — `forge_image --tamper`(1바이트 변조)→`[BL] ECDSA FAILED … refusing`→Safe State + `verify_decision` 단위 |
| 002 | unsigned 설치 | ✅ | **SIT-TC-07 on-target PASS** — `forge_image --unsign`(서명 64B=0)→size 정상이라 REQUIRED 경로 `ECDSA FAILED`(우회 불가) + 단위 |
| 003 | downgrade(낮은 버전) | ✅ | **SIT-TC-01 PASS** + `test_anti_rollback`(version_allowed) |
| 004 | ECU ID 불일치 이미지 | ✅(단위) | `test_anti_rollback`(ecu_id 4케이스) + 0x37 NRC 0x31 통합(ADR-009 코드리뷰). on-target 미실행 |
| 005 | Replay(이전 Transfer 재전송) | ⬜ | **미구현 — §19.1 L-1**(강한 seed freshness=ATECC608A 후속) |
| 006 | SecurityAccess 우회 RequestDownload | ✅(단위) | `test_uds_state`(미잠금/세션 거부 분기) |
| 007 | endless-data(size 초과) | ✅ | **SIT-TC-05 on-target PASS** — 작은 size 선언 후 초과 전송→`NRC SID=0x36 code=0x31`+세션종료 + FR-CAN-012/013 단위 2종(ISS-SEC-001) |
| 008 | CAN flood 중 업데이트 | ✅ | **SIT-TC-08 on-target PASS** — cangen 폭주 중 OTA→`Receive timeout`→메타 미commit→리셋 후 known-good v2(ESR=0, Bus-Off 없음). **S3 세션 타임아웃(FR-CAN-019) 구현**(uds_process, 단위 PASS) → 멈춘 세션 자동 abort |
| 009 | fake complete | 🔶 | fail-closed verify(**SIT-TC-03**·`verify_decision` 단위)로 commit 거부 경로 확인 |
| 010 | Campaign partial(한 ECU만) | ⬜ | Uptane-lite 로드맵(README ⬜계획) — §19.1 계열 |

요약: **✅ 7(001·002·003·004·006·007·008) + 🔶 1(009) + ⬜ 2(005·010).** ⬜ 2종(replay·campaign)은 HW(ATECC608A)/Uptane 로드맵으로 §19.1 연결. (2026-06-07 on-target으로 001·002·007·008 승격 — SIT-TC-05~08)

---

## 6. 요구사항 ↔ 결과 추적 (핵심)

| 요구사항 | 검증 수단 | 결과 |
|---|---|---|
| FR-AB-003 fail-closed | `test_bootloader_slot`(15) + SIT-TC-03 | PASS |
| FR-AB-004 self-test commit | `test_ota_meta`(self_confirm) + on-target(SIT-TC-01 경유) | PASS |
| FR-AB-007 3-strike | `test_meta_lifecycle`(plan_boot) + SIT-TC-02 | PASS |
| FR-BL-008 / FR-AB-008 anti-rollback | `test_anti_rollback` + SIT-TC-01 | PASS |
| FR-CAN-011 ECU 식별 | `test_anti_rollback`(ecu_id) + ADR-009 리뷰 | PASS(단위) |
| FR-CAN-010 SecurityAccess | `test_uds_state`(25)·`test_hmac`(3) + on-target unlock | PASS |
| FR-AB-005 메타 원자성 | `test_meta`·`test_ota_meta` | PASS |
| ISO 26262 센서 staleness | gcc(drive_sensor_fresh 6) + SIT-TC-04 | PASS |
| FR-CAN-012/013 endless-data | `test_uds_state`(누적초과 0x31·불완전 0x24, 2종) + SIT-TC-05 | PASS(단위) |
| FR-CAN-019 S3 타임아웃 / NFR-REL-003 / FR-BL-012 | `test_uds_state`(S3 abort·미abort 2종) + SIT-TC-08 | PASS(단위) |

### 6.1 계획 테스트명세(TS-OTA-001) ↔ 실행 결과 매핑

> TS-OTA-001(계획서, 28 케이스)은 작성 시점에 전부 N/T였다. 실제 검증은 **호스트 단위(Ceedling `test_*`)**·**on-target(SIT-TC-01~08)** 두 채널에서 다른 명명으로 수행됐다. 아래는 계획 TC를 실행 증거에 연결한 결과다(ASPICE SWE.6 — 계획↔결과 추적).
> 범례: ✅ 실증(전용 단위 케이스 또는 SIT-TC 직접) · 🔶 부분·간접(인접 테스트로 메커니즘 확인 또는 관측채널로 상시 사용, 전용 정량기록 없음) · ⬜ 미실행

| 계획 TC | 요구사항 | 실행 증거 | 상태 |
|---|---|---|:--:|
| TC-CAN-001 하트비트 200ms 주기 | FR-DRV-006 | 0x100 heartbeat가 SIT 관측채널로 상시 수신 확인. 주기 정량측정 전용 기록 없음 | 🔶 |
| TC-CAN-002 하트비트 페이로드 | FR-DRV-006 | byte[0]버전·byte[1]슬롯은 SIT-TC-01/02 판정에 사용. byte[2/3] 전용 확인 없음 | 🔶 |
| TC-CAN-003 SensorECU 0x200 장애물 | FR-SEN-002/004 | SIT-TC-04(0x200 분리 staleness) + gcc `drive_sensor_fresh` | ✅ |
| TC-CAN-004 DriveECU obstacle flag 반영 | FR-DRV-006/FR-SEN-002 | SIT-TC-04 경유 간접 확인 | 🔶 |
| TC-DRV-001 버튼 전진 | FR-DRV-002 | OTA 주행 시나리오 중 동작 관측, 전용 타이밍 기록 없음 | 🔶 |
| TC-DRV-002 시간 경과 자동 정지 | FR-DRV-003 | 주행 시나리오 중 관측, 전용 정량기록 없음 | 🔶 |
| TC-DRV-003 10cm 장애물 즉시 정지 | FR-DRV-003 | **SIT-TC-04 PASS** + gcc `drive_sensor_fresh`(6) | ✅ |
| TC-DRV-004 v2 정지 후 자동 후진 | FR-DRV-004 | v2 OTA 설치는 확인. v2 거동 전용 검증 미수행 | ⬜ |
| TC-DRV-005 OTA 중 주행 유지 | FR-DRV-007/NFR-SAFE-001 | OTA 푸시 중 주행 관측(SIT), 전용 기록 부분 | 🔶 |
| TC-OTA-001 ExtendedSession 진입 | FR-CAN-009 | `test_uds_state` + on-target OTA 세션 진입 | ✅ |
| TC-OTA-002 SecurityAccess unlock | FR-CAN-010 | `test_uds_state`·`test_hmac` + on-target unlock(§6) | ✅ |
| TC-OTA-003 잘못된 Key NRC | FR-CAN-010/016 | `test_uds_state` 거부 분기(invalidKey) | ✅ |
| TC-OTA-004 SA 없이 RDL NRC | FR-CAN-011/SR-ATK-006 | `test_uds_state` 미잠금 거부(=TC-ATK-006) | ✅ |
| TC-OTA-005 비활성 슬롯 자동 선택 | FR-CAN-011/FR-AB-002 | `test_bootloader_slot` + on-target OTA 슬롯 전환 관측 | ✅ |
| TC-OTA-006 블록 시퀀스 오류 NRC | FR-CAN-012 | `test_uds_state` 시퀀스 분기(단위) | ✅ |
| TC-OTA-007 TransferExit + pending | FR-CAN-013/FR-DRV-008 | `test_uds_state` 완료검증 + on-target OTA 완료 | ✅ |
| TC-OTA-008 IDLE 재부팅·슬롯 전환 | FR-DRV-008/FR-CICD-007 | **on-target SIT-TC-01/02**(슬롯 전환·v2 부팅) | ✅ |
| TC-SEC-001 ECDSA 정상 부팅 | FR-BL-007/SR-FW-002 | **SIT-TC-01**(version v2 OK) + `test_bootloader_slot`(verify_decision) | ✅ |
| TC-SEC-002 변조 펌웨어 차단 | SR-ATK-001/FR-BL-007 | **SIT-TC-06 PASS**(=TC-ATK-001) | ✅ |
| TC-SEC-003 미서명 펌웨어 차단 | SR-ATK-002/SR-FW-002 | **SIT-TC-07 PASS**(=TC-ATK-002) | ✅ |
| TC-SEC-004 DefaultSession SA NRC | FR-CAN-010/SR-ATK-006 | `test_uds_state` 세션 거부 분기 | ✅ |
| TC-SLOT-001 메타 없을 때 Slot A 기본 | FR-BL-002/FR-AB-001 | `test_meta`·`test_bootloader_slot`(기본 슬롯, 단위) | ✅ |
| TC-SLOT-002 A→B 전환 | FR-AB-002/FR-BL-003 | on-target OTA(SIT-TC-01/02 슬롯 전환 관측) | ✅ |
| TC-SLOT-003 B→A 재전환 | FR-AB-002 | A→B는 실증, 역방향 전용 기록 없음(대칭 로직) | 🔶 |
| TC-CI-001 git push 자동 트리거 | FR-CICD-002 | **Jenkins build #85** — git push(webhook 200)로 파이프라인 자동 시작(§4.1) | ✅ |
| TC-CI-002 변경 ECU 선택 빌드 | FR-CICD-003 | #85·#86 Detect `git diff v3 HEAD`→`drive=true sensor=true`로 per-ECU 스테이지 게이팅 실증. 단일 ECU만 변경 시 *제외(skip)* 케이스는 양쪽 변경 릴리스라 미격리 | 🔶 |
| TC-CI-003 E2E OTA(릴리스 배포) | FR-CICD-007/008 | **Jenkins build #86 PASS** — 빌드·ECDSA 서명·승인 게이트·양 ECU UDS OTA·A→B 슬롯전환·재부팅 확인 전 구간(§4.1). 버전 v4(흐름은 계획 v1→v2와 동일) | ✅ |
| TC-CI-004 OTA 실패 처리(FAILURE) | FR-CICD-010 | **Jenkins build #85** — 실 OTA 실패(SensorECU 무응답)→`OTA_EXIT=1`→파이프라인 FAILURE→이전 펌웨어 유지(fail-safe) 실증(§4.1). 의도적 CAN 차단 주입 방식은 후속 | ✅ |

**집계: ✅ 19 · 🔶 8 · ⬜ 1 (28종).** (2026-06-07 CI/CD on-target 실증으로 TC-CI-001·003·004 ✅ 승격 — §4.1 Jenkins build #85·#86.) ✅ 19종은 단위·on-target·CI/CD에서 직접 실증됐고, 🔶 8종은 메커니즘은 확인됐으나 전용 정량기록이 후속(주행 거동 타이밍·단일 ECU 선택빌드 격리 등)이며, ⬜ 1종(TC-DRV-004 v2 후진 거동)은 미실행으로 §7·§19.1에 연결한다.

---

## 7. 미실행·이연 항목 (Not Executed / Deferred)

| 항목 | 사유 | 연결 |
|---|---|---|
| TC-ATK-005 replay | 미구현(강한 freshness=HW 의존) | §19.1 L-1 |
| TC-ATK-010 campaign partial | Uptane-lite 로드맵 | §19.1 계열·README |
| SIT-TC-00 / -01b / -03b / -04b | 베이스라인·음성 대조 | SIT-001 §6 후속 |
| FR-CAN-018 RDBID(0x22, Should) | 미구현(Should) | — |
| TC-DRV-004 v2 후진 거동 | v2 OTA 설치는 확인, 거동 전용 검증 미수행 | §6.1 ⬜ |

모든 이연 항목은 **SRS §19.1 한계·잔여 위험 레지스터**와 정합하며, 본 결과서는 이를 PASS로 위장하지 않는다.

---

## 8. 결론

구현 범위(보안 6종 + 안전 1종)는 **단위 83/83 + on-target 8/8(기본 4 + 공격 4) 전부 PASS**로 검증됐다. 추가로 **호스트 퍼징(④, [FT-001](FT-001_Fuzz_Test_Plan_Report.md))으로 신규 결함 2건(F-002 sha256 UB·F-003 UDS 스택오버플로 CWE-121)을 발굴·차단**했다. 미구현·미실행은 §19.1에 잔여 위험으로 명시·수용했다.
**회귀(재현):** 단위는 `ceedling test:all`(83개)·`gcov:all`, on-target은 `tools/hil_runner.py --all`, 퍼징은 `fuzz/build.sh`+`fuzz/bin/<harness>`로 재현 가능. FAIL 발생 시 8D 트러블슈팅(ISS) 발행(PRC-006).

---

## 9. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-05 | 최초 — 호스트 단위 78/78·on-target 4/4 PASS 결과 기록, 보안 공격 시나리오(TC-ATK-001~010) 커버리지(✅3·🔶4·⬜3) 및 미실행·이연 항목의 §19.1 연결 매핑 포함 |
| 1.1 | 2026-06-06 | endless-data(TC-ATK-007) 정정 — FR-CAN-012 누적상한 + FR-CAN-013 완료검증이 문서엔 "구현"이나 코드 미반영이던 갭(ISS-SEC-001) 수정. 두 ECU uds.c 패치 + 단위 2종(총 80). 커버리지 ✅4·🔶3·⬜3로 갱신, on-target SIT-TC-05~08 §7 이연 |
| 1.2 | 2026-06-07 | on-target 하네스 보강 — SIT-TC-05~08을 `hil_runner --tc 05..08`로 자동/반자동화(`ota_client --declared-size`, `forge_image.py`, cangen 반자동). **§5 008(CAN flood) 과대표기 정정** — FR-CAN-019 S3 타임아웃이 문서엔 "구현"이나 코드 미반영 확인 → §7 이연 항목으로 추가(부분 OTA는 메타 미commit로 부팅 안전) |
| 1.3 | 2026-06-07 | **SIT-TC-01~04 on-target 재검증 4/4 PASS** — ABOM(ISS-CAN-006)·endless-data(FR-CAN-012/013) 펌웨어 변경 후 회귀 없음 확인(§4). 재검증 중 발견·해결: ISS-HW-001(ST-Link 글리치), ISS-SEC-002(PSK/구버전 클라이언트), ISS-OTA-006(고부하 FIFO, cf-delay 완화). 신규 SIT-TC-05~08은 후속 |
| 1.4 | 2026-06-07 | **신규 공격 시나리오 SIT-TC-05~08 on-target 4종 PASS** — endless-data(05)·변조(06)·미서명(07)·CAN flood no-brick(08). §5 커버리지 ✅7·🔶1·⬜2로 승격(001·002·007·008), §4 결과표 추가·§7 해당 이연 제거. on-target 누계 **8/8** |
| 1.5 | 2026-06-07 | **S3 세션 타임아웃(FR-CAN-019/NFR-REL-003/FR-BL-012) 구현** — 양 ECU `uds_process()`에 5s 무요청→세션 abort. 단위 2종(총 82). §5 008·§6 S3 행 추가, §7 FR-CAN-019 이연 제거. SRS에 FR-CAN-014(0x31)/015(0x11) 구현현황 주석·TC-FAIL-002 매핑 정정 동반 |
| 1.6 | 2026-06-07 | **계획 테스트명세(TS-OTA-001, 28종) ↔ 실행 결과 매핑** 신설(§6.1) — 단위/on-target 두 채널의 실행 증거를 28개 계획 TC에 연결(✅16·🔶10·⬜2). ⬜ 2종(TC-DRV-004·TC-CI-004) §7 이연 추가. TS-OTA-001 §13 집계·포인터 정합 |
| 1.7 | 2026-06-07 | **CI/CD 파이프라인 E2E 배포 on-target 실증(§4.1 신설)** — Jenkins build #86 v4 릴리스 전 구간 PASS(빌드·ECDSA 서명·승인 게이트·양 ECU UDS OTA·슬롯전환·재부팅 확인), build #85 git-push 자동트리거 + OTA 실패→FAILURE→fail-safe(FR-CICD-010) 실증. §1 캠페인 ③ 추가, §6.1 TC-CI-001·003·004 ✅ 승격(집계 ✅19·🔶8·⬜1), §7 TC-CI-004 이연 제거. TS-OTA-001 §13.1 동반 정합 |
| 1.8 | 2026-06-08 | **호스트 퍼징(④) 추가 + 단위 82→83** — FH-1/FH-3(libFuzzer+ASAN/UBSAN)로 신규 결함 2건 발굴·차단: F-002(sha256 부호시프트 UB)·F-003(UDS TransferData per-block 스택오버플로 CWE-121). test_uds_state +1(oversized), 원 PoC 재생 클린·재퍼징 1.4M회 무크래시. §1 캠페인 ④·§3 합계 갱신, 상세 [FT-001](FT-001_Fuzz_Test_Plan_Report.md)·ISS-SEC-003/004 |
