# ADR-010: 실보드 검증 명칭 — "HIL" 폐기, "시스템 통합 테스트(SIT)" 채택

| 항목 | 내용 |
|---|---|
| 문서 ID | ADR-010 |
| 상태 | accepted |
| 작성일 | 2026-06-06 |
| 관련 요구사항 | SIT-001(구 HIL-001), TR-001, ASPICE SWE.5/SWE.6, ISO 26262-6 |

---

## 1. Context (상황 · 문제 · 판단 기준)

실보드 검증 산출물을 `HIL-001`로 명명하고 테스트케이스를 `HIL-TC-NN`, 본문 용어를 "HIL"로 써 왔다. 그러나 **HIL(Hardware-in-the-Loop)의 정의적 요건은 plant(차량 동역학·센서·액추에이터·타 ECU)를 실시간 시뮬레이터로 대체**하는 것이다(dSPACE/NI/Vector 등). 검증 단계 스펙트럼(MIL→SIL→PIL→HIL)을 가르는 축은 *무엇이 실물이고 무엇이 시뮬레이션이냐* 하나이며, HIL의 구분점은 "plant = 실시간 시뮬레이션"이다.

본 프로젝트 벤치는 DriveECU·SensorECU·CAN 버스·모터·센서·RPi5 게이트웨이가 **전부 실물**이다. 시뮬레이션되는 plant가 0개이므로 정의상 HIL이 아니다 — 오히려 그 반대 구성이다(ST-Link 메타 위조·센서 분리는 *실 HW 위 fault injection*이지 plant 시뮬이 아니다).

**Decision Drivers:**
- **표준 정합성** — ASPICE SWE.5(SW 통합·통합시험)/SWE.6(SW 적격성시험), ISO 26262-6 용어와 일치해야 한다.
- **면접·리뷰 신뢰성(신입 전장 포트폴리오)** — HIL을 정확히 아는 평가자에게 "HIL" 오용은 역량 갭 신호다. 기존 `HIL-001`은 "엄밀한 HIL과 구분된다… 느슨한 약칭"이라는 디스클레이머를 달고 있었는데, **변명을 써야 한다는 것 자체가 용어 오류의 신호**다.

## 2. Considered Options (검토한 옵션)

- **옵션 A — `HIL` 유지 + 디스클레이머 강화.** 업계 통용 약칭이라 변명. 한계: 제목이 형성한 기대(실시간 시뮬레이터)와 본문이 충돌 → 평가자가 "용어를 몰랐거나 이력서용으로 늘렸다"로 해석. 정밀성이 역량 신호인 도메인에서 역효과.
- **옵션 B — `SIT`(System Integration Test)로 개명 [채택].** ASPICE SWE.5 정식 용어. ECU 2대 + 게이트웨이 + 버스 통합이라는 *실제 성격*과 정확히 일치.
- **옵션 C — 기타 후보.** `OTB`(On-Target Bench, 비표준 약어), `BT`(Bench Test, 단계 모호), `ONT`(On-Target, 시험 레벨 아닌 위치 표기). 의미는 맞으나 표준 명칭 가중치는 SIT가 가장 높다.

## 3. Decision (결정 · 근거)

**옵션 B 채택.** 문서 전반을 다음과 같이 표준화한다:

- 문서 ID `HIL-001` → **`SIT-001`**, 파일 `docs/test/SIT-001_System_Integration_Test_Plan.md`.
- 테스트케이스 `HIL-TC-NN` → **`SIT-TC-NN`**.
- 본문 용어 `HIL` → 문맥에 따라 **`on-target`**(검증 위치) 또는 **`SIT`**(시험 레벨). 단 "plant 시뮬레이터 기반 HIL이 *아니다*" 같은 **의도적 대조**에서는 HIL을 정확한 의미로 그대로 쓴다.
- 적용 범위: README, SDD-001, TR-001, SRS-001, diagram, ADR-002/007/009, ISS-CAN-005, PORTFOLIO_ONEPAGER, SIT-001 본체.

**코드 식별자는 변경하지 않는다(의도적 범위 제외):** `tools/hil_runner.py`(파일명), `forge_meta.py`/`build_fixtures.sh` 주석, 컴파일 매크로 `HIL_SELFTEST_FAIL`(빌드 스크립트 + 양 ECU `main.c`). 이유 — (1) 매크로명 변경은 펌웨어 재빌드·서명 픽스처 영향(blast radius), (2) 기능과 무관, (3) `HIL_SELFTEST_FAIL`은 *self-test 실패 주입* 의미라 시험 레벨 명칭과 별개. 문서에서 이들을 참조할 때는 실제 식별자 그대로(리터럴) 표기한다.

## 4. Consequences (결과 · 트레이드오프 · 한계)

| 판단 기준 | HIL 유지(A) | SIT 개명(B, 채택) |
|---|:---:|:---:|
| ASPICE/ISO 26262 용어 정합 | ✗ | ✅ |
| 면접·리뷰 신뢰성 | ✗(변명 필요) | ✅ |
| 산출물 일관성(SIT-001 24곳·SIT-TC 36곳) | — | ✅ |

- ✅ 표준 정합·면접 리스크 해소. 디스클레이머도 "왜 HIL이 아니라 SIT인지"를 표준 근거로 재작성.
- ⚠️ **문서-코드 용어 불일치 1곳 잔존:** `hil_runner.py`·`HIL_SELFTEST_FAIL`은 문서가 `SIT`로 부르는 대상의 코드 식별자다. 의도적 보류이며, 후속에서 `sit_runner.py` 등으로 정리 가능(별도 변경·재빌드 검증 동반).
- **표준:** ASPICE SWE.5(Software Integration and Integration Test)·SWE.6(Software Qualification Test), ISO 26262-6(소프트웨어 통합·시험), 검증 스펙트럼 MIL/SIL/PIL/HIL 정의.

## 5. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-06 | 최초 — HIL 오용 식별, SIT(ASPICE SWE.5)로 개명 채택. 문서 전반 `HIL`→`SIT/on-target`, `HIL-001`→`SIT-001`, `HIL-TC-NN`→`SIT-TC-NN` 교체. 코드 식별자(`hil_runner.py`·`HIL_SELFTEST_FAIL`)는 blast radius 고려해 보류 |
