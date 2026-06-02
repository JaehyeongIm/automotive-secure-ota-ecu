# ADR-005: 트러블슈팅 문서 파일명·ID 규칙

| 항목 | 내용 |
|---|---|
| 문서 ID | ADR-005 |
| 상태 | accepted |
| 작성일 | 2026-06-02 |
| 관련 요구사항 | docs/troubleshooting/, ASPICE SUP.9 (Problem Resolution Management) |

---

## 1. Context (상황 · 문제 · 판단 기준)

트러블슈팅 문서들이 본문 내부엔 `ISS-<영역>-<번호>` ID(ISS-CAN-003, ISS-OTA-004 …)를 쓰는데, **파일명은 `phaseN_…` / `툴이름_…`으로 제각각**이라 통일성이 떨어졌다. 일부 문서는 ID 자체가 없었다(phase4, phase5A, primask 실험).

**Decision Drivers (판단 기준):**
- ① 안정적 ID로 식별·정렬 가능할 것
- ② 파일명 ↔ 문서 내부 ID 일치(단일 출처)
- ③ `ADR-NNN_…` 명명 규칙과 시각적 통일
- ④ 추적성(ASPICE SUP.9의 problem ID, Jira 키와 같은 역할)

## 2. Considered Options (검토한 옵션)

- **옵션 A** — `ISS-<영역>-<NNN>_<kebab-slug>.md` (ID 우선, ADR과 통일). 장점: 정렬·검색·추적성↑, 일관성. 한계: 기존 파일 rename + 참조 갱신 필요.
- **옵션 B** — `phaseN_<설명>` 연대순 유지. 장점: 개발 타임라인 가시화. 한계: 통일성·검색성 낮고 ID와 불일치.
- **옵션 C** — 현행 유지. 한계: 불일치·혼란 지속.

## 3. Decision (결정 · 근거)

**옵션 A 채택.** 판단 기준 ①~④를 모두 만족하고 ADR 규칙과 통일된다.

- 영역 taxonomy(고정): **BL**(부트로더) · **CAN**(통신) · **OTA** · **SEC**(보안) · **IDE**(개발환경) · **RPI**(게이트웨이) · **HW**(하드웨어).
- 결함이 아닌 **실험·분석 문서**는 `EXP-<NNN>` 접두로 구분.
- 파일명의 ID = 문서 내부 ID와 동일. ID 없던 문서엔 부여(phase4 → ISS-BL-001, phase5A → ISS-BL-002, primask 실험 → EXP-001).
- phaseN 연대정보는 파일명에서 빠지되 문서 *내부* 헤딩(`## Phase 4 …`)으로 보존.
- 번호는 영역별 등장 순(gap 허용, Jira 키와 동일 철학). 새 이슈는 `_TEMPLATE.md`(8D)를 따라 다음 번호로 생성.

## 4. Consequences (결과 · 트레이드오프 · 한계)

9개 파일을 `git mv`로 rename(이력 보존)하고 참조를 갱신했다(`docs/etc/구현계획.md` + 문서 간 상호참조).

| 기존 파일명 | → 새 파일명 |
|---|---|
| local_cubeide_debug_setup.md | ISS-IDE-001_cubeide-debug-launch.md |
| phase4_driveECU_bootloader_can_interrupt.md | ISS-BL-001_can-irq-after-bootloader.md |
| phase5_bootloader_fallback_pending_invalid.md | ISS-BL-002_fallback-pending-invalid.md |
| phase5_slot_b_can_failure.md | ISS-CAN-003_slot-b-can-failure.md |
| phase8_sensorECU_ota_random_timeout.md | ISS-OTA-004_sensor-ota-random-timeout.md |
| phase8_no_heartbeat_after_failed_ota.md | ISS-OTA-005_no-heartbeat-after-failed-ota.md |
| phase9_rc_car_assembly_can_failure.md | ISS-CAN-004_rc-car-assembly-can-failure.md |
| rpi5_ssh_unreachable.md | ISS-RPI-001_rpi5-ssh-unreachable.md |
| primask_vtor_root_cause_isolation.md | EXP-001_primask-vtor-isolation.md |

- 트레이드오프: 한 번의 rename 비용(참조 갱신)으로 이후 일관성·추적성 확보.
- 한계: 번호가 영역별 비연속(gap)일 수 있음 — 의도된 동작(이슈 트래커와 동일).

## 5. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-02 | 최초 작성 — `ISS-<영역>-<NNN>_<slug>` / `EXP-<NNN>` 규칙 채택, 기존 9개 파일 rename |
