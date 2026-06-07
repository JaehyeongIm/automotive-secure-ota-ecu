# TARA-001: 위협 분석 및 리스크 평가 (Threat Analysis and Risk Assessment)

| 항목 | 내용 |
|---|---|
| 문서 ID | TARA-001 |
| 상태 | 분석 완료 (T-1~T-10 · SR-ATK·on-target SIT-TC 연결) |
| 버전 | 1.0 (개정이력은 §7) |
| 참조 표준 | ISO/SAE 21434:2021 (Clause 15, Threat analysis and risk assessment) |
| 관련 산출물 | SRS-001(SR-MF-*, SR-FW-*, SR-ATK-*, SR-KEY-*, SR-UP-*, FR-BL-006~009), HARA-001, ADR-004/006/009, SIT-001, TR-001, RTM-001 |

> **목적.** *의도적 공격자*가 침해할 수 있는 **자산**을 식별하고, 피해·위협·공격 실현가능성으로 **리스크**를 산정해 **Cybersecurity Goal과 보안 요구**를 도출한다. SRS의 SR-ATK-*/SR-FW-*는 *여기서 유도된 결과*로 추적된다(§3·§4).
> **v1.0:** T-1~T-10을 SR-ATK-001~010·on-target SIT-TC 결과와 연결해 분석을 닫았다. 저감(Reduce)된 위협은 SIT-TC PASS로 검증, 미저감 잔여(T-4 replay·T-10 campaign)는 [SRS §19.1] 잔여위험으로 *수용(Retain)*하고 트리거(ATECC608A·Uptane-lite)를 명시한다. 전체 요구 추적은 [RTM-001](RTM-001_Requirements_Traceability_Matrix.md).

---

## 1. 자산 경계 (Asset Boundary)

| 자산 | 보안 속성(C/I/A) | 왜 가치 있나 |
|---|---|---|
| A-1 비활성 슬롯 펌웨어 이미지 | **I** (무결성) | 변조 시 모터 거동을 임의 제어 가능 → 안전 위험 |
| A-2 SecurityAccess PSK (0x08007FE0) | **C** (기밀) | 유출 시 인증 우회 → 임의 업데이트 |
| A-3 ECDSA 개인키 (ECU 외부) | **C** | 유출 시 임의 펌웨어 서명 가능 → 신뢰사슬 붕괴 |
| A-4 부팅 가용성 / 기존 정상 App | **A** (가용) | 거부 시 차량 기능 상실 (DoS) |
| A-5 anti-rollback 버전 상태 | **I** | 우회 시 취약 구버전 재설치 |

> C/I/A = Confidentiality / Integrity / Availability. 피해 시나리오는 "이 속성이 깨지면 무엇이 나쁜가"로 쓴다.

---

## 2. 평가 척도 (ISO/SAE 21434)

**(a) 영향 등급 (Impact) — 4개 범주별로 평가, 최댓값 채택**

| 범주 | Severe | Major | Moderate | Negligible |
|---|---|---|---|---|
| **Safety (S)** | 사망/치명상 | 중상 | 경상 | 무상 *(HARA-001 위험 사건 등급 참조)* |
| **Financial (F)** | 회복 불가 손실 | 큰 손실 | 감내 가능 | 무시 가능 |
| **Operational (O)** | 핵심 기능 상실 | 기능 저하 | 경미 | 무시 가능 |
| **Privacy (P)** | 다수·민감정보 | 식별 가능 | 제한적 | 무시 가능 |

**(b) 공격 실현가능성 (Attack Feasibility) — Attack Potential 방식 (21434 Annex / ISO 18045)**

5개 인자를 합산해 등급화: **소요시간(Elapsed Time)**, **전문성(Expertise)**, **아이템 지식(Knowledge)**, **기회의 창(Window of Opportunity)**, **장비(Equipment)**.
→ 종합 **High / Medium / Low / Very Low** (낮을수록 공격이 어려움). *(대안: CVSS 기반 산정도 21434에서 허용)*

**(c) 리스크 결정 (Risk = Impact × Feasibility, 값 1~5)** — *프로젝트 정의 매트릭스(21434는 자체 정의 허용, 단 문서화 필수)*

| Impact \ Feas | Very Low | Low | Medium | High |
|---|:--:|:--:|:--:|:--:|
| **Severe** | 2 | 3 | 4 | **5** |
| **Major** | 1 | 2 | 3 | 4 |
| **Moderate** | 1 | 2 | 2 | 3 |
| **Negligible** | 1 | 1 | 1 | 2 |

**(d) 리스크 처리 (Risk Treatment):** Avoid(회피) / **Reduce(저감)** / Share(전가) / Retain(수용). 처리 결정 → **Cybersecurity Goal** → 보안 요구.

---

## 3. TARA 분석표

| ID | 자산 | 피해 시나리오 (CIA) | 위협 (STRIDE) | 공격 경로 | Impact | Feas | **Risk** | 처리 | CG | 검증 / 잔여 (SR-ATK) |
|---|---|---|---|---|:--:|:--:|:--:|---|:--:|---|
| **T-1** | A-1 펌웨어 무결성 | 변조 펌웨어 실행→모터 폭주→충돌(**S:Severe**,O:Major) | Tampering | CAN→0x34→0x36→슬롯 기록→재부팅 | Severe | Medium | **4** | Reduce | CG-1 | ✅ SIT-TC-06(ECDSA FAILED)·SR-ATK-001 |
| T-2 | A-2 PSK | PSK 유출→인증 우회→임의 업데이트(C) | Info Disclosure | SWD/플래시 readout, 비보호영역 저장 | Severe | Low | **3** | Reduce | CG-2 | 🔶 WRP 영역 격리(SR-KEY-003, 칩 read 확인). **잔여:** RDP/Secure Element 미적용→물리 추출 가능, ADR-006 후속 |
| T-3 | A-5 버전 상태 | 취약 구버전 재설치(I) | Tampering | 낮은 fw_version 서명 이미지 주입 | Major | Medium | **3** | Reduce | CG-3 | ✅ SIT-TC-01(anti-rollback)·SR-ATK-003. **잔여:** 기준선 CRC 메타(변조 시 한계)→ATECC608A |
| T-4 | A-2/A-1 | 캡처한 SecurityAccess/Transfer 재전송(I) | Spoofing/Replay | 이전 세션 Seed/Key·블록 캡처 후 재전송 | Major | Medium | **3** | **Reduce 목표→Retain** | CG-4 | ⬜ **미저감(잔여 수용)** — 강한 freshness=HW(TRNG/ATECC608A) 의존. 현완화: SW nonce·세션상태·S3. §19.1 L-1·ADR-004/006·SR-ATK-005 |
| T-5 | A-4 가용성 | CAN Flood로 OTA/주행 방해(A) | DoS | 버스 포화, Erase 구간 충돌 | Major(O) | Medium | **3** | Reduce | CG-5 | ✅ SIT-TC-08(no-brick)+S3 abort(FR-CAN-019)+ABOM(ISS-CAN-006)·SR-ATK-008 |
| T-6 | A-1 | 타 ECU용 이미지 설치→오거동(S/O) | Tampering/Spoofing | Drive 이미지를 Sensor에 OTA | Major | Low | **2** | Reduce | CG-6 | ✅ 서명 헤더 target_ecu_id(ADR-009)·test_anti_rollback·SR-ATK-004. 잔여: 직접 플래시(UDS 우회)는 BL defense-in-depth 후속 |
| T-7 | A-2 | 미인증/무차별 업데이트 시작(I) | Spoofing/EoP | SecurityAccess Key 추측·반복 시도 | Major | Low | **2** | Reduce | CG-7 | ✅ HMAC-SHA256(PSK,Seed)+3회 잠금(test_uds_state)·SR-ATK-006 |
| T-8 | A-4/A-1 | 누적 초과 전송→인접 flash 손상(I/A) | Tampering/DoS | 작은 size 선언 후 초과 블록 전송 | Major | Medium | **3** | Reduce | CG-8 | ✅ SIT-TC-05(누적상한 NRC 0x31, FR-CAN-012/013)·ISS-SEC-001·SR-ATK-007 |
| T-9 | A-1 | GW의 가짜 완료로 미검증 commit(I) | Spoofing | 게이트웨이 fake 0x77/complete 명령 | Severe | Medium | **4** | Reduce | CG-9 | 🔶 ECU 자체검증(fail-closed, SIT-TC-03)·자가확정으로 GW 불신뢰(SR-UP-004)·SR-ATK-009. 전용 fake-complete 케이스 미실행 |
| T-10 | A-4 | 일부 ECU만 업데이트→차량 불일치(O) | Tampering/Repudiation | 캠페인 중 1개 ECU만 성공 | Moderate | Low | **2** | **Reduce 목표→Retain** | CG-10 | ⬜ campaign 추적 미구현 — Uptane-lite 후속·§19.1·SR-ATK-010 |

**T-1 등급 근거 (워크드 예시 — 모든 행은 동일 방식):**
- **Impact = Severe** — 모터 폭주는 HARA-001 H-1/H-2의 충돌 위험으로 직결(Safety:Severe). 안전↔보안 브리지의 핵심 사례.
- **Feasibility = Medium** — 소요시간: 중 / 전문성: Proficient(UDS·ISO-TP) / 지식: Public(표준) / 기회의 창: 차량 내부 CAN 물리접근 필요(제한적) / 장비: Standard(CANable+오픈툴). 원격이 아닌 *물리 버스 접근*이 병목 → High가 아닌 Medium.
- → **Severe × Medium = Risk 4** → **Reduce** → CG-1 → CSR-1.x(§4) → SIT-TC-06 PASS로 검증.

**잔여 위험(Retain) 결정 근거:**
- **T-4 Replay (Risk 3):** 강한 freshness는 하드웨어 TRNG/단조 카운터 부재로 미달. SW nonce(UID+tick+ctr→SHA-256)+세션상태+S3로 *부분 완화*하나 재부팅 replay 창이 남음. **ATECC608A 입고 시 해소**(ADR-006) — 그 전까지 [SRS §19.1 L-1]로 수용.
- **T-10 Partial Campaign (Risk 2):** 다중 ECU 캠페인 조율(Uptane Director)은 단일 게이트웨이 벤치 범위 밖 → Uptane-lite 후속으로 수용. no-brick(T-5)로 *부분 설치 자체가 차량을 망가뜨리지 않음*은 보장.

---

## 4. Cybersecurity Goal → 보안 요구 내려보내기 (V의 왼쪽)

T-1을 끝까지 내려본 예시. **기존 SR-* 요구에 톱다운 뿌리를 단다.**

| 고도 | 항목 | 내용 | 추적 (기존 SRS) |
|---|---|---|---|
| 위협 | T-1 | 변조 펌웨어 주입, Risk 4 | 본 문서 §3 |
| **Cybersecurity Goal** | CG-1 | 검증 안 된 펌웨어는 설치/실행 안 됨 | — |
| 보안 요구 | CSR-1.1 | 부트로더는 (헤더+코드) ECDSA-P256 서명을 부팅 전 검증 | **FR-BL-007, SR-FW-002, SR-ATK-002** |
| 보안 요구 | CSR-1.2 | 서명으로 보호되는 Image Header로 독립 검증(Gateway 불신뢰) | **FR-AB-008, SR-UP-004** |
| 보안 요구 | CSR-1.3 | `image_size==0` 등 검증 우회 경로 부재(fail-closed) | **FR-AB-003** |
| 보안 요구 | CSR-1.4 | 최종 이미지 SHA-256를 ECDSA가 흡수 검증(별도 Manifest hash 없음, §8.1) | **SR-FW-001, SR-MF-005** |
| 설계/구현 | — | `uECC_verify()`, `ota_img_header_read()`, `forge_image.py`(시험) | bootloader.c |
| **검증(V 오른쪽)** | on-target | 변조 1바이트 거부 / 미서명 거부 / size==0 우회 거부 | **SIT-TC-06·SIT-TC-07·SIT-TC-03 PASS** (구 TC-SEC→TC-ATK 개명) |

> **드러난 갭:** T-1은 SR-ATK-002·SR-FW-002·FR-AB-008·FR-AB-003 **여러 요구를 하나의 목표(CG-1) 아래로 묶는다.** 지금 SRS엔 이것들이 흩어져 있어 — TARA가 *왜 이 요구들이 함께 존재하는지*의 근거를 만든다. 또한 CSR-1.3(우회 차단)은 위협을 돌려야 비로소 "size==0 경로"가 보인다.

### 4.1 Cybersecurity Goal 요약 (CG → 요구 → 검증 → 상태)

T-1~T-10이 도출한 보안 목표를 기존 SR/FR 요구·검증과 연결(전체 추적은 [RTM-001](RTM-001_Requirements_Traceability_Matrix.md)).

| CG | 목표 | 주요 요구 | 검증 | 상태 |
|---|---|---|---|:--:|
| CG-1 | 미서명/변조 펌웨어 미실행 | FR-BL-007·SR-FW-002·FR-AB-003·SR-ATK-001/002 | SIT-TC-06/07·test_bootloader_slot | ✅ |
| CG-2 | PSK 보호영역 격리 | SR-KEY-003·FR-BL-013(WRP) | 칩 read·코드리뷰 | 🔶 RDP/Secure Element 후속 |
| CG-3 | 다운그레이드 거부 | FR-BL-008·SR-FW-003·SR-ATK-003 | SIT-TC-01·test_anti_rollback | ✅ |
| CG-4 | 세션 인증 재사용 거부 | FR-CAN-017·SR-ATK-005 | — | ⬜ 잔여(HW freshness) |
| CG-5 | 통신중단/포화 안전복구 | FR-CAN-019·NFR-REL-003·SR-ATK-008 | SIT-TC-08·test_uds_state(S3) | ✅ |
| CG-6 | 타 ECU 이미지 거부 | SR-FW-004·FR-CAN-011·SR-ATK-004 | ADR-009·test_anti_rollback | ✅ |
| CG-7 | 미인증 업데이트 차단 | FR-CAN-010·SR-ATK-006 | test_uds_state(3회 잠금) | ✅ |
| CG-8 | endless-data 차단 | FR-CAN-012/013·SR-ATK-007 | SIT-TC-05·test_uds_state | ✅ |
| CG-9 | GW fake-complete 무력화(ECU 자체확정) | FR-AB-004·SR-UP-004·SR-ATK-009 | SIT-TC-03(fail-closed) | 🔶 |
| CG-10 | 캠페인 일관성 | SR-UP-003·SR-MF-007·SR-ATK-010 | — | ⬜ Uptane-lite |

→ **8/10 목표가 ✅ 검증**(on-target SIT-TC 또는 단위), CG-2/9 부분(🔶), CG-4/10 잔여 수용(⬜). 잔여 2건은 모두 §19.1 등재.

---

## 5. 안전↔보안 브리지

T-1의 **Safety 영향(Severe)** 은 [HARA-001](HARA-001_Hazard_Analysis_Risk_Assessment.md)의 H-1/H-2 위험 사건에서 가져온 값이다. 즉 보안 리스크 산정이 안전 분석을 *입력으로 참조*한다. **공격(TARA) → 해(HARA)** 사슬이 닫혀야 "Secure OTA"의 존재 이유가 증명된다.

---

## 6. 채우는 순서 (사용법)

1. §1에서 자산과 C/I/A 속성을 확정. "지키려는 게 뭔가"부터.
2. 자산별 **피해 시나리오**(속성이 깨지면 뭐가 나쁜가) → §2(a)로 4범주 영향 평가. Safety는 HARA 참조.
3. **위협 시나리오**(어떻게 깨나, STRIDE 분류) + **공격 경로**(네 UDS/ISO-TP 흐름으로) 작성.
4. §2(b) 5인자로 **실현가능성** 등급 + 근거 한 줄(T-1처럼). "원격이냐 물리냐"가 보통 결정적.
5. §2(c) 매트릭스로 Risk → 처리 결정 → **Cybersecurity Goal**.
6. §4처럼 Goal을 보안 요구로 내려 **기존 SR-* ID에 연결**. 연결 안 되는 SR-*는 근거 미상 → 재검토, 새로 나온 요구(CSR-1.3 등)는 SRS 반영.

---

## 7. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 0.1 | 2026-06-03 | 최초 템플릿 — 영향/실현가능성/리스크 척도, T-1 워크드 예시, T-2~5 stub, CG-1 수직 추적, HARA 브리지 |
| 1.0 | 2026-06-07 | **분석 완료** — T-2~T-5 등급 채움 + T-6~T-10 신규(SR-ATK-001~010 전수 대응). 각 위협을 Impact×Feas로 Risk 산정·처리(Reduce/Retain) 결정, **on-target SIT-TC 검증 결과 연결**(8/10 ✅·CG-2/9 🔶·CG-4 replay/CG-10 campaign ⬜ 잔여수용). §4.1 CG 요약표(목표→요구→검증→상태) 신설, 구 `TC-SEC`→`SIT-TC` 참조 정정, CSR-1.4 Manifest hash→ECDSA 흡수(§8.1) 반영, RTM-001 링크 |
