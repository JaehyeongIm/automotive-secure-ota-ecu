# TR-001: 소프트웨어 테스트 결과서 (Test Report)

| 항목 | 내용 |
|---|---|
| 문서 ID | TR-001 |
| 문서명 | Secure OTA ECU 시스템 소프트웨어 테스트 결과서 |
| 레벨 | SW 검증 결과 보고(ASPICE SWE.6 — Test Report) |
| 작성일 | 2026-06-05 |
| 대상 | Bootloader + DriveECU/SensorECU App + 공유 코어(ota_meta·crypto·uds) |
| 참조 | SRS-001(v2.12), TEST_SPEC_OTA v1.0(계획), HIL-001(on-target 플랜·기록), SDD-001, project.yml |
| 표준 근거 | ISO 26262-6(검증), ISO/SAE 21434(보안검증·잔여위험), ASPICE SWE.5/6 |

> 본 결과서는 *실제로 실행한* 테스트의 **결과**를 기록한다. *계획*(TEST_SPEC·HIL-001 플랜)과 구분되며,
> 미실행·이연 항목은 단정하지 않고 **SRS §19.1 한계·잔여 위험 레지스터**로 연결한다.

---

## 1. 요약 (Verdict)

| 캠페인 | 범위 | 결과 |
|---|---|---|
| ① 호스트 단위테스트(Ceedling) | HAL 분리 *순수 코어* 로직 | **78/78 PASS** · 라인 ~91%·분기 ~79% |
| ② On-target 벤치(HIL-001) | 실 ECU 2대·실 CAN·실 OTA | **4/4 PASS** (보안 3 + 안전 1) |

**판정: 구현 범위 PASS.** 미구현·미실행(replay·campaign·HIL 음성대조 등)은 **§19.1 잔여 위험으로 명시·수용**한다(과대 PASS 주장 없음).
실행일 — 단위: 2026-06-05(본 결과서 작성 시 재실행 확인), on-target: 2026-06-04.

---

## 2. 테스트 환경

| 구분 | 구성 |
|---|---|
| 단위(①) | Ceedling 0.31 / Ruby 2.6, gcc 호스트(macOS). `project.yml :source` = `SensorECU/Core/Src` + `Bootloader/Core/Src`(HAL 분리 순수코어). 실행 `ceedling test:<name>` · 커버리지 `ceedling gcov:all`(strict: taken-at-least-once) |
| on-target(②) | STM32F446RE ×2(Drive/Sensor) + ST-Link + CAN 트랜시버/버스 + RPi5 게이트웨이. 자극·관측 오케스트레이션 `tools/hil_runner.py`, OTA `ota_client.py`(운영 경로 재사용), 메타 위조 `forge_meta.py`+st-flash. 상세 HIL-001 §1 |

> 단위 범위 한계: `:source`가 Sensor/Bootloader만 컴파일 → DriveECU 고유 로직(`drive.c`)은 gcc 독립검증(6케이스)로 별도 확인(SDD §8). 본 보고서 단위 수치에는 미포함.

---

## 3. ① 호스트 단위테스트 결과 — 78/78 PASS

| 테스트 파일 | 케이스 | 결과 | 검증 대상(코어) |
|---|---:|---|---|
| `test_anti_rollback` | 10 | PASS | `version_allowed`·`img_header_read`·`ecu_id_allowed`(FR-BL-008/AB-008·FR-CAN-011) |
| `test_bootloader_slot` | 15 | PASS | `verify_decision`(fail-closed REQUIRED/REFUSE/SKIP)·슬롯 선택(FR-AB-003) |
| `test_hmac` | 3 | PASS | HMAC-SHA256 RFC 4231 벡터(FR-CAN-010) |
| `test_meta` | 7 | PASS | 메타 CRC32·`ota_meta_select`(FR-AB-005) |
| `test_meta_lifecycle` | 8 | PASS | `plan_boot`(증가-먼저·3-strike)·`plan_confirm`(FR-AB-007) |
| `test_ota_meta` | 10 | PASS | `write_pending`·`self_confirm`(RAM 하니스, FR-AB-004) |
| `test_uds_state` | 25 | PASS | SecurityAccess·세션·시퀀스·**거부 분기**(음성)(FR-CAN-009~016) |
| **합계** | **78** | **PASS (0 Failures)** | |

**커버리지(`gcov:all`, strict):** 라인 **~91%** · 분기 **~79%**. 코어별 실측 — `ota_flash` 98.3%L/73.7%B · `sha256` 94.4%L/83.3%B · `uds` 86.3%L/77.5%B · `hmac_sha256` 77.3%L/75.0%B. (HAL/페리페럴·startup 파일은 단위 범위 밖 — gcov "no coverage" 정상)

---

## 4. ② On-target 벤치 결과 (HIL-001) — 4/4 PASS

| TC | 요구사항 | 핵심 관측 로그 | 결과 | 일시 |
|---|---|---|---|---|
| HIL-TC-01 anti-rollback | FR-BL-008 / FR-AB-008 | `anti-rollback: v1 below baseline — refusing` → `rollback to slot 0` → `version v2 OK` | **PASS** | 2026-06-04 |
| HIL-TC-02 3-strike 롤백 | FR-AB-007 | `trial start` → `trial retry`×2 → `3-strike … INVALID, rollback` → `version v2 OK` | **PASS** | 2026-06-04 |
| HIL-TC-03 fail-closed | FR-AB-003 | `metadata size invalid (0x00000000) — fail-closed, refusing` → `Safe State` | **PASS** | 2026-06-04 |
| HIL-TC-04 센서 staleness | ISO 26262 안전상태 | `[DRIVE] 센서 stale → fail-safe 정지`(센서 0x200 분리 후 ~150ms) | **PASS** | 2026-06-04 |

- 실증 경로: 실 OTA(35400 B, ISO-TP/UDS)로 전달 → 부트로더 검증·전이·거부 사유를 UART/CAN heartbeat로 자동 대조(HIL-001 §6).
- **미실행:** HIL-TC-00(베이스라인)·HIL-TC-01b/03b/04b(음성 대조) → 후속(HIL-001 §6 기록과 일치).

---

## 5. 보안 공격 시나리오 커버리지 (TC-ATK-001~010 ↔ SR-ATK)

> 범례: ✅ 실증(실행 증거 있음) · 🔶 부분/간접(인접 실행 테스트로 메커니즘 확인, 전용 케이스 미실행) · ⬜ 미실행/미구현(§19.1)

| TC-ATK | 시나리오 | 상태 | 근거 / 비고 |
|---|---|---|---|
| 001 | Firmware 변조(hash) | 🔶 | ECDSA/SHA 검증 게이트 on-target 동작(HIL-TC-01의 `ECDSA OK` 경유)·`verify_decision` 단위. 전용 1바이트 변조주입 케이스 미실행 |
| 002 | unsigned 설치 | 🔶 | 부트로더 검증 REQUIRED 게이트(단위)·secure boot 경로 on-target. 전용 미서명 주입 케이스 미실행 |
| 003 | downgrade(낮은 버전) | ✅ | **HIL-TC-01 PASS** + `test_anti_rollback`(version_allowed) |
| 004 | ECU ID 불일치 이미지 | ✅(단위) | `test_anti_rollback`(ecu_id 4케이스) + 0x37 NRC 0x31 통합(ADR-009 코드리뷰). on-target 미실행 |
| 005 | Replay(이전 Transfer 재전송) | ⬜ | **미구현 — §19.1 L-1**(강한 seed freshness=ATECC608A 후속) |
| 006 | SecurityAccess 우회 RequestDownload | ✅(단위) | `test_uds_state`(미잠금/세션 거부 분기) |
| 007 | endless-data(size 초과) | 🔶 | FR-CAN-012 누적상한(SR-ATK-007) 구현. 전용 부하 케이스 미실행 |
| 008 | CAN flood 중 업데이트 | ⬜ | S3 타임아웃(FR-CAN-019) 구현, 부하 시험 후속 |
| 009 | fake complete | 🔶 | fail-closed verify(**HIL-TC-03**·`verify_decision` 단위)로 commit 거부 경로 확인 |
| 010 | Campaign partial(한 ECU만) | ⬜ | Uptane-lite 로드맵(README ⬜계획) — §19.1 계열 |

요약: **✅ 3(003·004·006) + 🔶 4(001·002·007·009) + ⬜ 3(005·008·010).** ⬜ 3종은 전부 §19.1/로드맵으로 연결.

---

## 6. 요구사항 ↔ 결과 추적 (핵심)

| 요구사항 | 검증 수단 | 결과 |
|---|---|---|
| FR-AB-003 fail-closed | `test_bootloader_slot`(15) + HIL-TC-03 | PASS |
| FR-AB-004 self-test commit | `test_ota_meta`(self_confirm) + HIL(TC-01 경유) | PASS |
| FR-AB-007 3-strike | `test_meta_lifecycle`(plan_boot) + HIL-TC-02 | PASS |
| FR-BL-008 / FR-AB-008 anti-rollback | `test_anti_rollback` + HIL-TC-01 | PASS |
| FR-CAN-011 ECU 식별 | `test_anti_rollback`(ecu_id) + ADR-009 리뷰 | PASS(단위) |
| FR-CAN-010 SecurityAccess | `test_uds_state`(25)·`test_hmac`(3) + HIL unlock | PASS |
| FR-AB-005 메타 원자성 | `test_meta`·`test_ota_meta` | PASS |
| ISO 26262 센서 staleness | gcc(drive_sensor_fresh 6) + HIL-TC-04 | PASS |

---

## 7. 미실행·이연 항목 (Not Executed / Deferred)

| 항목 | 사유 | 연결 |
|---|---|---|
| TC-ATK-005 replay | 미구현(강한 freshness=HW 의존) | §19.1 L-1 |
| TC-ATK-008 CAN flood | 부하 시험 미수행 | 후속 |
| TC-ATK-010 campaign partial | Uptane-lite 로드맵 | §19.1 계열·README |
| HIL-TC-00 / -01b / -03b / -04b | 베이스라인·음성 대조 | HIL-001 §6 후속 |
| FR-CAN-018 RDBID(0x22, Should) | 미구현(Should) | — |

모든 이연 항목은 **SRS §19.1 한계·잔여 위험 레지스터**와 정합하며, 본 결과서는 이를 PASS로 위장하지 않는다.

---

## 8. 결론

구현 범위(보안 6종 + 안전 1종)는 **단위 78/78 + on-target 4/4 전부 PASS**로 검증됐다. 미구현·미실행은 §19.1에 잔여 위험으로 명시·수용했다.
**회귀(재현):** 단위는 `ceedling test:all`(78개)·`gcov:all`, on-target은 `tools/hil_runner.py --all`로 재현 가능. FAIL 발생 시 8D 트러블슈팅(ISS) 발행(PRC-006).

---

## 9. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-05 | 최초 — 호스트 단위 78/78·on-target(HIL) 4/4 PASS 결과 기록, 보안 공격 시나리오(TC-ATK-001~010) 커버리지(✅3·🔶4·⬜3) 및 미실행·이연 항목의 §19.1 연결 매핑 포함 |
