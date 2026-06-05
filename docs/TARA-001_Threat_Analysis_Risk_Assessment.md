# TARA-001: 위협 분석 및 리스크 평가 (Threat Analysis and Risk Assessment)

| 항목 | 내용 |
|---|---|
| 문서 ID | TARA-001 |
| 상태 | Draft (템플릿 — 워크드 예시 1건 + stub) |
| 버전 | 0.1 (개정이력은 §7) |
| 참조 표준 | ISO/SAE 21434:2021 (Clause 15, Threat analysis and risk assessment) |
| 관련 산출물 | SRS-001(SR-MF-*, SR-FW-*, SR-ATK-*, SR-KEY-*, SR-UP-*, FR-BL-006~009), HARA-001, ADR-004/006 |

> **목적.** *의도적 공격자*가 침해할 수 있는 **자산**을 식별하고, 피해·위협·공격 실현가능성으로 **리스크**를 산정해 **Cybersecurity Goal과 보안 요구**를 도출한다. SRS의 SR-ATK-*/SR-FW-*는 *여기서 유도된 결과*여야 한다 — 지금은 "떠올린 공격 나열"이라 뿌리가 없다.

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

| ID | 자산 | 피해 시나리오 (CIA) | 위협 시나리오 (STRIDE) | 공격 경로 | Impact | Feas | **Risk** | 처리 | Cybersecurity Goal |
|---|---|---|---|---|:--:|:--:|:--:|---|---|
| **T-1** | A-1 펌웨어 무결성 | 변조 펌웨어 설치·실행 → 모터 폭주 → 충돌(**S:Severe**, O:Major) | **Tampering** | CAN→0x10→0x34→0x36→비활성 슬롯 기록→재부팅 | Severe | Medium | **4** | Reduce | CG-1: 검증되지 않은(미서명) 펌웨어는 설치/실행되지 않는다 |
| T-2 | A-2 PSK | PSK 유출로 인증 우회 → 임의 업데이트 (C) | **Info Disclosure** | App 슬롯/메모리에서 PSK 추출, 또는 비보호 영역 저장 | _TODO_ | _TODO_ | _TODO_ | Reduce | CG-2: PSK는 WRP 보호영역에만 존재하고 App에 노출되지 않는다 |
| T-3 | A-5 버전 상태 | 취약 구버전 재설치 (I) | **Tampering** | 낮은 fw_version 이미지 주입 | _TODO_ | _TODO_ | _TODO_ | Reduce | CG-3: Confirmed 버전보다 낮은 이미지는 거부된다 |
| T-4 | A-1/A-2 | 캡처한 SecurityAccess Key 재전송 (I) | **Spoofing/Replay** | 이전 세션 Seed/Key 캡처 후 재전송 | _TODO_ | _TODO_ | _TODO_ | Reduce | CG-4: 이전 세션 인증의 재사용은 거부된다 |
| T-5 | A-4 가용성 | CAN Flood로 OTA/주행 방해 (A) | **DoS** | 버스 포화, Erase 구간 충돌 유도 | _TODO_ | _TODO_ | _TODO_ | Reduce | CG-5: 통신 중단/포화 시 기존 App으로 안전 복구된다 |

**T-1 등급 근거 (워크드 예시):**
- **Impact = Severe** — 모터 폭주는 HARA-001 H-1/H-2의 충돌 위험으로 직결(Safety:Severe). 안전↔보안 브리지의 핵심 사례.
- **Feasibility = Medium** — 소요시간: 중 / 전문성: Proficient(UDS·ISO-TP 이해) / 지식: Public(표준 프로토콜) / 기회의 창: 차량 내부 CAN 물리접근 필요(제한적) / 장비: Standard(CANable+오픈툴). 원격이 아니라 *물리 버스 접근*이 병목 → High가 아닌 Medium.
- → 매트릭스 **Severe × Medium = Risk 4** → **Reduce**.

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
| 보안 요구 | CSR-1.4 | 최종 이미지 SHA-256 == Manifest hash | **SR-FW-001, SR-MF-005** |
| 설계/구현 | — | `uECC_verify()`, 헤더 파서 | (구현 단계) |
| **검증(V 오른쪽)** | TC | 변조 1바이트 거부 / 미서명 거부 / size==0 우회 시도 거부 | **TC-SEC-002, TC-SEC-003** |

> **드러난 갭:** T-1은 SR-ATK-002·SR-FW-002·FR-AB-008·FR-AB-003 **여러 요구를 하나의 목표(CG-1) 아래로 묶는다.** 지금 SRS엔 이것들이 흩어져 있어 — TARA가 *왜 이 요구들이 함께 존재하는지*의 근거를 만든다. 또한 CSR-1.3(우회 차단)은 위협을 돌려야 비로소 "size==0 경로"가 보인다.

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
