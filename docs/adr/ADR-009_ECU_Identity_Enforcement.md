# ADR-009: ECU 식별 강제 — 타 ECU용 이미지 거부의 위치·정체성 출처

| 항목 | 내용 |
|---|---|
| 문서 ID | ADR-009 |
| 상태 | accepted (앱 레벨 채택 — 부트로더 defense-in-depth는 후속) |
| 작성일 | 2026-06-05 |
| 관련 요구사항 | FR-CAN-011, FR-AB-008, FR-BL-009, SR-UP-004, ADR-007 |

---

## 1. Context (상황 · 문제 · 판단 기준)

서명 이미지 헤더(ADR-007)에 `target_ecu_id` 필드(1=DRIVE, 2=SENSOR)가 ECDSA 서명영역으로 **포함**되고 서명 도구도 값을 박지만(`sign_firmware.py --ecu-id`, 파이프라인이 drive=1/sensor=2 주입), **어느 컴포넌트도 그 값을 비교·거부하지 않았다.** 즉 "신원증은 발급하나 입구에서 검사하지 않는" 상태였다(FR-CAN-011 미구현).

**막으려는 위협:** DriveECU용 펌웨어(`target_ecu_id=1`)가 실수/악의로 **SensorECU 보드**(2여야 함)에 설치되어 모터 제어 코드가 센서 보드에서 동작 → 오작동. 헤더의 ecu_id는 서명에 묶여 **위조 불가**하므로, *받는 쪽이 자기 정체성과 비교*만 하면 차단된다.

**핵심 판단축:** 거부하는 주체가 *자기가 어느 ECU인지*를 어디서 아는가(= 정체성 출처). 이것이 코드 위치·brick 위험·검증성을 좌우한다.

## 2. 결정 1 — 어디서 강제하나 (정체성 출처)

- **옵션 A — 앱 레벨(uds.c) + 컴파일타임 ID [채택]**
  검사 주체 = 현재 동작 중인 앱. **DriveECU/SensorECU 앱은 애초에 별도 코드베이스**라 자기 정체성을 컴파일타임에 안다(`#define OTA_ECU_ID OTA_ECU_ID_DRIVE|SENSOR`). 헤더의 `target_ecu_id` ≠ 자기 ID면 OTA를 거부. **표준 정위치**(ISO 14229 RequestDownload/Transfer fail-fast). brick 위험 0(부트로더 불변), 순수 결정함수로 단위테스트 가능.
- **옵션 B — 부트로더 레벨(TOFU)**
  검사 주체 = 부트로더. 그러나 **부트로더는 두 보드에 동일 바이너리 하나**가 올라가 컴파일 상수를 못 쓴다. 정체성을 *현재 CONFIRMED 슬롯의 서명 헤더 ecu_id*에서 런타임 추론해야 한다. 장점: 실행 직전 차단 → **UDS 우회(직접 플래시)도 차단**(verify-before-execute). 단점: TOFU(공장 첫 이미지가 정체성 확정 전까지 검사 불가), 로직 복잡·약간 순환적.

**채택: 옵션 A.** 정체성이 모호하지 않고(가장 큰 혼란원 제거), 부트로더를 건드리지 않아 안전하며, ecu_id 검사의 표준 정위치(RequestDownload 계열 fail-fast)에 해당하고, 단위테스트로 "타 ECU 이미지 거부"를 증명할 수 있다.

## 3. 결정 2 — 거부 지점·정책

- **지점:** `RequestTransferExit`(0x37) — uds.c가 이미 서명 헤더를 파싱해 `fw_version`(anti-rollback 기준선)을 읽는 바로 그 자리. 동일 헤더의 `target_ecu_id`를 함께 검사해, 불일치 시 **메타 commit 전에** `NRC 0x31(requestOutOfRange)`로 거부(이미지를 pending/부팅대상으로 만들지 않음).
- **순수 함수:** `ota_meta_ecu_id_allowed(header_ecu_id, my_ecu_id)` (ota_meta.c, SSOT 3곳) — `header_ecu_id == 0 || header_ecu_id == my_ecu_id`. anti-rollback `ota_meta_version_allowed`와 동일한 "순수 결정함수 분리→단위테스트" 패턴.
- **`header_ecu_id == 0`(미지정) 허용:** `--ecu-id` 없이 서명한 이미지(default 0)와의 호환. 안전한 이유 = ecu_id는 서명영역이라 공격자가 0으로 위조하려면 개인키가 필요(이미 더 큰 문제). 헤더 자체가 없으면(레거시) 검사 스킵 — anti-rollback과 동일 정책.

## 4. Consequences (결과 · 트레이드오프 · 한계)

| 위협 | 앱 레벨 ecu_id 검사로 차단되나 |
|---|---|
| 실수로 타 ECU 이미지 OTA | ✅ (NRC 0x31 거부) |
| 원격 CAN 공격자(정상 OTA 경로) | ✅ (ecu_id 서명 위조 불가 + 비교) |
| 물리/ST-Link 직접 플래시(UDS 우회) | ❌ 앱이 관여 안 함 → **부트로더 defense-in-depth 후속** |

- ⚠️ **UDS 우회 한계:** 검사 주체가 *앱*이라, UDS 다운로드를 거치지 않고 플래시에 직접 굽는 경로는 못 막는다. 정석 보강 = 부트로더가 점프 직전 한 번 더 검사(옵션 B). 부트로더 정체성은 ATECC608A(ADR-006)의 프로비저닝 슬롯 또는 per-ECU 빌드로 해소 가능 → "지금은 가능선(앱), 정석은 후속"의 ADR-003/004/007 계열 구조와 동일.
- ⚠️ **검증 한계:** 순수 결정함수(`ota_meta_ecu_id_allowed`)는 호스트 단위테스트로 검증(거부/허용/미지정/미래ID 4케이스). 단, uds.c의 *통합 글루*(0x37에서 `g_fw_addr` 헤더 읽기)는 실 플래시 주소 deref라 호스트에서 도달 불가 → 인접한 fw_version 읽기와 마찬가지로 HIL/코드리뷰로 검증.
- **표준:** ISO 14229 RequestDownload 식별자 검증(FR-CAN-011), Uptane ECU-targeting(SR-UP-004, 각 ECU는 자신을 대상으로 한 이미지만 수용), FR-AB-008 서명 헤더.

## 5. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-05 | 최초 — 앱 레벨 컴파일타임 ID 강제 채택(uds.c 0x37, NRC 0x31), 순수 함수 `ota_meta_ecu_id_allowed`(단위 4케이스), 부트로더 TOFU(옵션 B) 기각·UDS 우회 차단은 defense-in-depth 후속으로 기록 |
