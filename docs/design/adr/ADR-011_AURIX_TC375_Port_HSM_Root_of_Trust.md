# ADR-011: AURIX TC375 포팅 — 온칩 HSM으로 신뢰체인 [0]·[1] 확보 (ADR-006 옵션 C 진행)

| 항목 | 내용 |
|---|---|
| 문서 ID | ADR-011 |
| 상태 | proposed (하드웨어 주문 전 · TC375 Lite Kit 주문 예정 · 첫 마일스톤=secure-boot 슬라이스) |
| 작성일 | 2026-06-18 |
| 관련 요구사항 | ADR-006(옵션 C), ADR-007, ADR-003, ADR-004, SR-KEY-003, SR-ATK-005, FR-BL-008, ISO/SAE 21434, UN R155/R156, ISO 26262-6 |

---

## 1. Context (상황 · 문제 · 판단 기준)

현재 부트 신뢰체인의 **하단 두 칸이 비어 있다.** STM32F446에는 온칩 HSM이 없어, 부트로더는 WRP(쓰기보호)로만 지켜진다. WRP는 "남이 못 덮어쓰게" 막을 뿐, **부팅 순간 부트로더가 진짜인지 암호로 검증하지 않는다** — 즉 신뢰의 뿌리가 SW(부트로더) 자신이고, 그 밑에 불변(immutable) 검증자가 없다.

| 신뢰체인 칸 | 현재 STM32F4 | 목표(AURIX TC375) |
|---|---|---|
| [0] 불변 루트 | **없음** (하드웨어 루트 부재) | HSM 부트 ROM + OTP 루트키 |
| [1] 부트로더 검증 | **없음** (WRP 쓰기보호뿐, 암호검증 X) | HSM이 부팅 시 부트로더 서명검증 |
| [2] 앱 이미지 검증 | 있음 — SW `uECC`+`sha256` ECDSA | 있음 — **HSM 하드웨어**로 오프로드 |

**ADR-006**은 이미 "옵션 C — 온칩 HSM 내장 자동차 MCU(AURIX/S32K3/Stellar)"를 검토했고, **"양산 정석이나 데모 범위 초과"로 보류**한 뒤 외부 Secure Element(ATECC608A)로 절충했다. 그러나 ADR-006 §4 한계 2가 명시하듯 **외부 I2C 버스는 변조 방지가 아니다**(스니핑/스푸핑 가능). 온칩 HSM만이 [0]·[1]을 닫고 변조 방지를 제공한다. 본 ADR은 ADR-006이 보류한 **옵션 C를 실제로 진행**하는 결정이다.

**키 계층(무엇이 어디로 가나):** 본 프로젝트엔 역할이 다른 세 키가 있으며, 포팅 시 계층이 명확해진다.

| 키 | 종류 | 검증 대상 | 신뢰체인 위치 | 포팅 시 변화 |
|---|---|---|---|---|
| PSK | 대칭 HMAC-SHA256 | OTA 클라이언트(세션 접근권) | 세션 계층(체인과 직교) | WRP 플래시 → HSM 보안저장(SR-KEY-003 강화) |
| ECDSA 공개키 | 비대칭 | 펌웨어 작성자(이미지 서명) | [2] | SW 검증 → HSM 검증·보안저장 |
| OTP 루트키 | 대칭(SHE BOOT_MAC) 또는 비대칭(PKC) | 부트로더 자신 | [0]→[1] | **신규** — UCB_HSMCOTP에 프로비저닝 |

**Decision Drivers (판단 기준):**
- ① 신뢰체인 [0]·[1]을 채워 **하드웨어 신뢰앵커 기반 secure boot** 완성
- ② ADR-006이 외부 SE로만 절충한 변조방지·보안저장을 **온칩으로 격상**
- ③ 표준 정합 — SHE/EVITA(HSM), ISO/SAE 21434·UN R156(SW 업데이트), ISO 26262 ASIL-D(락스텝)
- ④ 이식 공수·리스크 관리(플랫폼 전면 변경) — 범위를 통제할 것

## 2. Considered Options (검토한 옵션)

- **옵션 A — STM32F4 + ATECC608A 유지(ADR-006 현행).** 추가비용 0, 동작·검증 완료. 한계: [0]·[1] 미충족, 외부 I2C라 변조방지 아님 → 신뢰체인 하단 공백 잔존.
- **옵션 B — STM32H5 등 ARM 온칩 보안 MCU.** 온칩 secure boot 제공, **ARM 계열이라 이식 마찰 최소**(Cortex-M 멘탈모델 유지). 한계: 자동차 등급·ASIL·CAN-FD·HSM 격이 AURIX보다 약함(산업용 기반).
- **옵션 C — AURIX TC375(온칩 HSM·락스텝·CAN-FD) [채택 방향].** 양산 정석. HSM 신뢰앵커 + ASIL-D + CAN-FD. 한계: TriCore 툴체인(Tasking/HighTec)·iLLD·멀티코어·CSA·ENDINIT·read-while-write 플래시 → **이식 공수 최대**.

**스코프 결정(중요):** 옵션 C 내에서 *풀포팅* vs *secure-boot 슬라이스 우선*. 신뢰체인 임팩트의 대부분은 [0]·[1]에서 나오고 [2]는 이미 보유하므로, **첫 마일스톤을 슬라이스로 한정**해 blast radius를 관리한다(UDS 스택 전체 이식은 후속).

## 3. Decision (결정 · 근거)

**옵션 C 채택 — AURIX TC375를 secure OTA 부트체인의 타깃 플랫폼으로 삼는다.** 단 풀포팅이 아니라 **secure-boot 슬라이스 우선**으로 진행한다.

**조달(주문 예정):**
- Infineon **AURIX TC375 lite Kit** (KIT_A2G_TC375_LITE)
- **DAS + miniWiggler/DAP** (플래시·디버그; 기존 ST-Link 대체)
- 툴체인: AURIX Development Studio + Tasking/HighTec, iLLD

**첫 마일스톤 — secure-boot 슬라이스([0]·[1] + [2] 오프로드):**
1. **[0]** `UCB_HSMCOTP`에 OTP 루트키 프로비저닝(1회성·비가역 — 학습 후 확정).
2. **[1]** HSM이 부팅 시 부트로더 서명검증(SHE BOOT_MAC AES-CMAC, 또는 PKC 지원 시 비대칭).
3. **[2]** 부트로더가 OTAH 이미지 ECDSA 검증을 **HSM에 위임**(`uECC`/`sha256` SW → HSM HW; AUTOSAR Csm/CryIf 경유 가능).
4. **키 이전** PSK(HMAC) + ECDSA 공개키 → HSM 보안저장(SR-KEY-003 강화).
5. A/B 슬롯·3-strike 폴백 등 상위 OTA 상태머신 로직은 STM32판에서 이식(플랫폼 독립).

**조달 전 확정할 미결 항목:**
- **TC375 HSM의 PKC(비대칭) 지원 여부** → ECDSA를 HSM 하드웨어로 검증할지, HSM 펌웨어 SW로 검증할지 결정([2] 설계에 직접 영향). 데이터시트 출처로 확인.

## 4. Consequences (결과 · 트레이드오프 · 한계)

- ✅ **신뢰체인 [0]·[1] 완성** — 부트로더 밑에 불변 하드웨어 검증자 확보. ADR-006 §4 한계 2(외부버스 변조방지 아님) 해소.
- ✅ **ADR-003/004도 더 정석적으로 해결** — ATECC608A(외부 SE)의 TRNG·NV 카운터를 **온칩 HSM TRNG·보안NV**로 대체 가능. ATECC608A는 학습 산출물로 남고 양산 방향은 HSM으로 수렴.
- ✅ **표준 정합** — SHE/EVITA(HSM 등급), ISO/SAE 21434·UN R155/R156, AUTOSAR Crypto Stack(Csm/CryIf), ISO 26262 ASIL-D(락스텝·SMU·메모리 ECC).
- ⚠️ **트레이드오프(공수·리스크):** TriCore 툴체인·iLLD·멀티코어·CSA·ENDINIT·read-while-write 플래시 → 이식 공수 최대. **풀포팅이 아니라 슬라이스 우선으로 통제**한다.
- ⚠️ **STM32판 병행 유지:** 동작·검증된 기준 산출물(SIT-001/TR-001)은 그대로 두고, AURIX는 별도 타깃으로 둔다(둘 중 하나로 회귀 금지).
- **후속:** 데이터시트 PKC 확인 → 슬라이스 WBS 수립 → 통합 후 ADR-006/003/004/007 상태 재평가(역참조 연결).

## 5. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-18 | 최초 — AURIX TC375 채택(ADR-006 옵션 C 진행). 신뢰체인 [0]·[1] 공백 식별, secure-boot 슬라이스 우선 스코프, 키 계층(PSK/ECDSA/OTP 루트키) 정리, PKC 지원 여부를 조달 전 미결 항목으로 명시 |
