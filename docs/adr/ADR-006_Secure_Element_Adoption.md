# ADR-006: Secure Element(ATECC608A) 도입 — F446 무TRNG·무EEPROM 한계의 하드웨어 해소

| 항목 | 내용 |
|---|---|
| 문서 ID | ADR-006 |
| 상태 | accepted (부품 주문 완료 · 통합 대기) |
| 작성일 | 2026-06-02 |
| 관련 요구사항 | FR-CAN-010, FR-BL-008, SR-ATK-003/005/006, SR-KEY-003, ADR-003, ADR-004, ADR-007 |

---

## 1. Context (상황 · 문제 · 판단 기준)

STM32F446RE에는 **하드웨어 TRNG·EEPROM·HSM이 없다.** 그래서 SecurityAccess의 두 요소를 소프트웨어로 우회하고 각 ADR에 한계를 명시해 두었다.
- **ADR-004 (Seed):** TRNG가 없어 `SHA-256(UID‖tick‖counter)` nonce로 대체 → 엔트로피 약함·재부팅 replay 한계(NIST SP 800-90 DRBG 미준수).
- **ADR-003 (잠금):** EEPROM이 없어 잠금 상태를 RAM에 보관 → 전원 리셋 시 우회 가능(ISO 14229 "리셋에도 유지" 미충족).

이 두 한계를 *근본적으로* 닫으려면 보안 하드웨어가 필요하다.

**Decision Drivers (판단 기준):**
- ① ADR-004(진짜 TRNG seed) · ADR-003(NV 잠금)을 **동시에** 해결할 것
- ② F446에 추가 가능할 것(I2C·3.3V, 기존 보드 유지)
- ③ 데모/포트폴리오 규모 — 예산(2만 원대)·입수성·학습가치
- ④ 표준 정합 방향(SHE/HSM, NIST SP 800-90 TRNG)

## 2. Considered Options (검토한 옵션)

- **옵션 A — 현행 유지** (내부 SW nonce + RAM 잠금). 장점: 추가비용 0. 한계: ADR-003/004 근본 미해결.
- **옵션 B — 외부 Secure Element (ATECC608A)**. SparkFun Qwiic Cryptographic Co-Processor `DEV-18077`. 내장 **TRNG + monotonic counter + 보안 키저장 + ECDSA/SHA/HMAC**, I2C. 장점: 003·004·키저장을 한 칩으로, 저비용·학습가치↑. 한계: 부품·I2C·CryptoAuthLib 포팅·프로비저닝 공수, 외부 버스라 변조 방지 아님.
- **옵션 C — HSM 내장 자동차 MCU 교체** (Infineon AURIX, NXP S32K3, ST Stellar 등). 장점: 양산 정석(온칩 TRNG+SHE+보안NV+secure boot). 한계: 기판·플랫폼 전면 변경 → 데모 범위 초과.

## 3. Decision (결정 · 근거)

**옵션 B 채택.** 판단 기준 ①~④를 충족하며, 옵션 C(양산 정석)의 방향을 *데모 규모로* 선취한다.

**조달(주문 완료):**
- SparkFun **DEV-18077** (ATECC608A, I2C/Qwiic)
- GIN LING **2.54mm male 핀헤더 1×40** (보드 8핀 PTH 납땜 → F-F 점퍼로 F446 연결)

**해결 매핑** — *서로 다른 약점*을 ATECC608A의 *각기 다른 기능*으로 대응한다(한 문제를 한 번에 푸는 것이 아님):
| 약점 | 실패 원인 (필요 성질) | ATECC608A 기능 | CryptoAuthLib API |
|---|---|---|---|
| ADR-004 Seed 예측가능 | 엔트로피원 부재 (**난수성**) | 하드웨어 TRNG | `atcab_random()` |
| ADR-003 잠금 리셋 우회 | RAM **휘발성** (지속성) | 보안 NV 슬롯 / 카운터 | `atcab_counter()` · `atcab_write_zone()` |
| anti-rollback 기준선 위조 (FR-BL-008, ADR-007) | CRC-only 메타라 **위조 가능** (변조방지) | monotonic counter = rollback index | `atcab_counter()` |
| PSK 저장 (SR-KEY-003 강화) | 앱 슬롯/WRP 한계 | 보안 키 슬롯 | `atcab_write/read_zone()` |

> **이 셋은 서로 다른 실패 원인**(엔트로피 / 휘발성 / 변조)을 가지며, ATECC608A가 *공통 안전 저장소·난수원*이라 각각을 *다른 기능으로* 막는 것뿐이다 — 같은 문제라서 한꺼번에 해결되는 게 아니다.
>
> ⚠️ **범위 주의:** ATECC608A는 *거기 저장한 값(난수·rollback 카운터·잠금·키)만* 보호한다. 메타데이터 *전체*(슬롯 상태 등)는 여전히 CRC-only 플래시라 위조 가능하다 — CRC는 우연한 손상만 검출하지 변조는 못 막는다. 메타 *전반*의 변조 검출까지 원하면 **ATECC608A 키로 메타에 MAC(인증 태그)를 부착**해야 하며, 이는 별도 설계로 자동 따라오지 않는다.

**연결·통합:**
- HW: I2C1(`PB8`=SCL / `PB9`=SDA) + 3V3 + GND. Qwiic 보드 내장 풀업(외부 저항 불필요), 3.3V(레벨시프트 불필요).
- SW: Microchip **CryptoAuthLib** + STM32 HAL I2C shim.
- 통합 단계: ① I2C 인식(`atcab_init`) → ② TRNG→seed(ADR-004 해소) → ③ counter→잠금(ADR-003 해소) → ④(선택) PSK 슬롯 이전.
- 프로비저닝 주의: config/data zone **lock은 1회성·비가역** → 초기엔 unlocked로 학습 후 lock.

## 4. Consequences (결과 · 트레이드오프 · 한계)

- 통합 완료 시 **ADR-003·004의 "개선 경로"가 실제로 닫힘.** 두 ADR의 관련 항목에 ADR-006을 역참조로 연결.
- **트레이드오프:** 부품·I2C·CryptoAuthLib 포팅·프로비저닝 학습 공수가 든다(그 자체가 학습가치이기도 함).
- **한계 1 — 608A vs 608B:** 신규 *설계*는 608B 권장이나, 학습/데모 용도엔 기능 차이 없음.
- **한계 2 — 변조 방지 아님:** 외부 I2C 버스라 물리 공격자가 스니핑/스푸핑 가능. *온칩* HSM/SHE(옵션 C)가 양산 정석 — 본 결정은 데모 범위의 절충임을 명시.
- **후속:** 통합 후 ADR-003/004의 상태(잠정) 재평가, ATECC608A 미장착 시 SW 폴백 동작 유지 여부 결정.

## 5. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-02 | 최초 작성 — ATECC608A(DEV-18077) 도입 결정, 부품 주문 완료, 통합 단계·해결 매핑 기록 |
| 1.1 | 2026-06-03 | 해결 매핑 정정 — 잠금(휘발성)·anti-rollback(변조)·seed(엔트로피)는 *서로 다른 약점*임을 명시(실패원인 열 추가), anti-rollback rollback-index 행 추가(FR-BL-008/ADR-007), 메타 *전체* 무결성은 별도 MAC 필요·CRC≠변조방지 명시 |
