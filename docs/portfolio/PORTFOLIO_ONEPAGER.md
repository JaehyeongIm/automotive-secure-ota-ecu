# Automotive Secure OTA ECU — 1‑Page 요약

> **CAN 버스 Dual‑ECU 환경의 UDS/ISO‑TP Secure OTA 파이프라인**을 직접 구현하고,
> 보안·안전 핵심 기능을 **실 RC 차량(on‑target)으로 실증**한 임베디드 펌웨어 프로젝트.

🎬 **데모** [youtu.be/_RbVwU_nPHI](https://youtu.be/_RbVwU_nPHI)  ·  📂 **코드/문서** 본 저장소  ·  🧰 STM32F446RE ×2 + RPi5 + CAN

---

## 핵심 지표 (KPI)

| | |
|---|---|
| 🔐 보안 6종 | Secure Boot(ECDSA‑P256) · Anti‑rollback · Fail‑closed 검증게이팅 · SecurityAccess(HMAC) · 3‑strike 롤백 · 메타 이중화 원자성 |
| 🛡️ 안전 1종 | 센서 staleness fail‑safe (ISO 26262 안전상태 전이) |
| ✅ 단위 테스트 82개 | 라인 커버리지 91% · 분기 79%(strict) — 양성 + 음성(거부경로) |
| 🔬 On‑target 8/8 PASS | 실 OTA·실 CAN·실 RC차 — 기본 4(보안 3+안전 1) + 공격 4종(변조·미서명·endless-data·flood) |
| 📄 표준 산출물 | SRS · SDD · HARA · TARA · SIT · TR + ADR×10 + 8D×14 (ASPICE SWE.1~6 추적) |

---

## 무엇을·어떻게 (기능 ↔ 표준 ↔ 검증)

| 기능 | 설계 핵심 | 표준 근거 | 검증 |
|---|---|---|---|
| Secure Boot | SHA‑256 + ECDSA‑P256(uECC 직접 포팅), WRP 잠금 부트로더 | ISO 24089, 신뢰 사슬 | 단위 + on-target |
| Anti‑rollback | 앞 서명헤더 `fw_version` vs CONFIRMED 기준선, 거부 시 graceful 롤백 | UNECE R155, AVB rollback index | **SIT TC‑01** |
| 3‑strike 롤백 | 시험부팅(증가‑먼저‑점프) 3회 초과 → INVALID + 자동 롤백 | ISO 24089, FTTI | **SIT TC‑02** |
| Fail‑closed 게이팅 | 메타 `size` 비정상 시 *검증 우회 차단*(허용 아닌 거부) | CWE‑636, deny‑by‑default | **SIT TC‑03** |
| 메타 원자성 | 섹터 이중화 + CRC32 + seq, ping‑pong(전원차단 안전) | AUTOSAR Fee/NvM | 단위 |
| SecurityAccess | Key=HMAC‑SHA256(PSK,Seed)[:4], 3회 실패 → 잠금 | ISO 14229 0x27, RFC 2104 | 단위 + on-target |
| 센서 staleness | 0x200 수신 타임아웃(150ms) → fail‑safe 정지 | ISO 26262, AUTOSAR E2E | **SIT TC‑04** |
| CI/배포 분리 | push=CI / 태그 vN + `input` 승인 게이트 → 배포 | UN R156 SUMS, Uptane Image/Director | Jenkins |

---

## 검증 전략 — V‑모델 양방향

- **① 호스트 단위(`ceedling gcov:all`)** — HAL과 분리한 *순수 코어*(메타 상태머신·anti‑rollback·검증게이팅·crypto)를 82개 테스트로. *음성 테스트*로 거부 분기까지(분기 79%).
- **② On‑target 벤치(SIT‑001)** — 실 ECU 2대·실 CAN·실 OTA로 보안 3 + 안전 1을 실증(4/4 PASS). *plant 시뮬레이터 기반 HIL이 아닌 실물 환경*임을 명시.

---

## 핵심 설계 결정 (ADR 발췌)

- **ADR‑007** anti‑rollback: 앞 서명헤더 + 메타(CRC) 기준선 — *지금 가능선*, 정석은 Secure Element.
- **ADR‑003/004/006** SecurityAccess 잠금=RAM·seed=SW nonce의 한계를 *명시*하고 **ATECC608A**(TRNG·monotonic counter)로 해소 결정.
- **ADR‑008** CI 빌드와 차량 배포를 분리 + 승인 게이트 — "push=배포"의 blast‑radius 문제 차단.

> 한계를 *숨기지 않고 ADR로 박제*하고 후속 경로(Secure Element)를 제시 — 엔지니어링 정직성·로드맵 사고.

---

## 엔지니어링 스토리 (차별점)

1. **갭 발견** — README/SRS가 *구현됐다고 명세한* 보안기능 다수가 실제 코드엔 없음을 코드 대조로 적발.
2. **요구 강화** — 실무자가 던지는 적대적 질문(신뢰경계·원자성·측정가능성·fail‑closed)으로 SRS를 재작성.
3. **TDD 구현** — HAL/순수 분리 → 순수 코어를 테스트 먼저, 부트로더·앱에 통합.
4. **On‑target 실증** — 운영 OTA 경로(Mac→Pi→CAN) 그대로 실보드 검증, 검증 중 결함 2건(anti‑rollback halt, ISR printf 폭주) 추가 발견·수정.

→ *요구↔설계↔코드↔테스트* 추적성을 양방향으로 닫음.

---

## 기술 스택

`C` (bare‑metal) · STM32F446RE/HAL · **Custom Bootloader** · CAN Classic · **UDS(14229)/ISO‑TP(15765)** ·
ECDSA‑P256/SHA‑256/HMAC · **Ceedling/Unity/CMock**(+gcov) · Python(OTA·on-target 오케스트레이션) ·
**Jenkins CI/CD** · ISO 24089/26262, UNECE R155/156, ASPICE, AUTOSAR(개념)
