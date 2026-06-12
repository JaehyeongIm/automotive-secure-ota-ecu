# ADR-004: SecurityAccess Seed 난수 생성 — TRNG 부재에 따른 표준(NIST SP 800-90) 미준수

| 항목 | 내용 |
|---|---|
| 문서 ID | ADR-004 |
| 상태 | 확정 — reboot-replay(freshness)는 영속 boot-epoch로 해소·on-target TC-ATK-005 PASS(2026-06-13, §6); 엔트로피(TRNG 부재)·물리 변조는 후속(옵션 B/C) |
| 작성일 | 2026-06-02 |
| 관련 요구사항 | FR-CAN-010, SR-ATK-005, SRS §13.6, ADR-006 |

---

## 1. 배경

SecurityAccess 챌린지-응답은 매 세션 ECU가 **Seed(4바이트)**를 발급하고, 게이트웨이가 `HMAC-SHA256(PSK, Seed)`로 Key를 만들어 응답한다(SRS §13.6). Seed의 역할은 **freshness** — 이전 세션에서 캡처한 Key의 재전송(replay)을 막는 것(SR-ATK-005)이다. 따라서 Seed는 세션마다 *예측 불가능하고 중복되지 않아야* 한다.

표준 난수 생성은 **물리 엔트로피원(TRNG) → DRBG**의 2계층(NIST SP 800-90)인데, **STM32F446RE에는 하드웨어 TRNG도, HSM/SHE도 없다.** 즉 표준대로 Seed를 만들 하드웨어 수단이 없다.

## 2. 검토한 옵션

### 옵션 A: 소프트웨어 nonce (현재 채택, 잠정)

`Seed = SHA-256(UID || SysTick || counter)[0:4]`

- **장점:** 추가 HW 없이 즉시 구현, counter로 부팅 내 세션 간 중복 방지.
- **한계:** **엔트로피원이 없다** — UID는 칩 상수(0비트), counter는 예측가능(0비트), SysTick만 몇 비트. SHA-256은 엔트로피를 *생성하지 못하고 혼합만* 한다. → **표준 DRBG가 아니며** Seed가 예측 가능하다. 게다가 counter가 RAM이라 재부팅 시 초기화되어, 부팅 직후 Seed가 이전 부팅의 초기 Seed와 반복 → **재부팅 후 replay**가 가능하다.

### 옵션 B: 온칩 물리 잡음 엔트로피 수확 → HMAC_DRBG

ADC 잡음(플로팅/내부 채널 LSB), 내부 온도센서·VREFINT LSB, 또는 독립 발진기 지터(LSI RC vs HSE 타이머 캡처)에서 엔트로피를 모아, SP 800-90B 건전성 테스트 후 **HMAC_DRBG(SP 800-90A)**를 시드한다.

- **장점:** 추가 HW 없이 표준에 근접. **한계:** 엔트로피 품질 검증 필요, 구현·검증 공수.

### 옵션 C: 외부 보안소자/HSM (SHE, ATECC608 등)

TRNG + 키 저장을 내장한 부품을 추가한다.

- **장점:** 프로덕션 정석. **한계:** BOM·기판 변경 → 데모 범위 초과.

---

## 3. 표준 분석

- **NIST SP 800-90B**(엔트로피원) → **800-90A**(DRBG: HMAC_DRBG / CTR_DRBG / Hash_DRBG) → **800-90C**(결합)가 난수 생성의 업계 표준이며, **ISO/SAE 21434**·**AUTOSAR Crypto**가 이를 채택한다.
- 자동차 실무: **AUTOSAR Csm `RandomGenerate`** 프리미티브가 MCU TRNG 또는 **HSM/SHE**를 백엔드로 사용한다. 실제 ECU는 Seed를 HSM 내부에서 생성한다.
- 근본 원칙: **"해시로 난수를 만들 수 없다 — 엔트로피는 물리원에서만 나온다."** 옵션 A가 표준을 못 따르는 이유가 바로 이것이다(엔트로피원 부재).

---

## 4. 최종 결정: 옵션 A 잠정 채택 (표준 미준수 명시)

| 판단 기준 | A (SW nonce) | B (지터→HMAC_DRBG) | C (HSM/SHE) |
|---|:---:|:---:|:---:|
| Seed 예측 불가(표준 정합) | ✗ | △~○ | ✓ |
| 추가 HW 불필요 | ✓ | ✓ | ✗ |
| 구현 즉시성 | ✓ | △ | ✗ |
| 데모 범위 적합 | ✓ | △ | ✗ |

### 근거
- 데모 범위·일정상 A를 채택하되, **표준 미준수임을 명시적으로 인정**한다(SRS §13.6 정정).
- **위협모델 완화:** 보안의 본질은 *Seed가 아니라 PSK*에 있다(Seed는 평문 공개 챌린지). replay-after-reboot는 *재부팅 능력*이 필요한데 원격 CAN 공격자는 불가능하고(물리 공격자는 이미 ST-Link로 더 큰 침해가 가능하며 진짜 부팅 관문은 ECDSA), 4바이트 Key지만 HMAC + 3회/10s 잠금으로 온라인 추측이 차단된다.
- **개선 경로:** 1순위 후속은 **옵션 B**(ADC/발진기 지터 → HMAC_DRBG), 프로덕션은 **옵션 C**(HSM/SHE).

---

## 5. 제약 사항 및 한계

1. ~~재부팅 후 Seed 재사용으로 **replay 가능**~~ → **§6에서 해소**(영속 boot-epoch). Seed는 여전히 예측 가능·낮은 엔트로피지만, replay 방어엔 *freshness(비반복)* 만 필요하고 그것은 충족된다(예측불가능성은 별도 속성 — 옵션 B/C 후속).
2. **용어 정정:** 본 구현은 NIST SP 800-90A의 "DRBG"가 아니라 *엔트로피원 없는 nonce 생성기*다. SRS §13.6의 "소프트웨어 DRBG" 표기를 이에 맞게 정정한다.
3. ~~counter가 RAM이라 재부팅 시 단조성 미보장~~ → **§6에서 해소**(영속 `seq_counter` 기반 boot-epoch, 부팅 가로질러 단조 증가).
4. 본 ADR은 *잠정* 결정이며, 옵션 B(엔트로피원) 도입 시 재평가한다. **freshness/replay 부분은 §6으로 종결.**

---

## 6. 갱신 (2026-06-13): 영속 boot-epoch로 reboot-replay 해소

§5 한계 #1·#3의 **재부팅 replay**를 옵션 A 골격을 유지한 채 닫았다.

**원리.** replay 방어는 *freshness(비반복)* 만 필요하다 — `Key=HMAC(PSK,Seed)`라 Seed가 예측 가능해도 PSK 없이는 Key를 못 만든다. 따라서 Seed가 *재부팅을 가로질러 반복만 안 하면* 충분하다(엔트로피 불필요). AUTOSAR **SecOC의 Freshness Value(단조 카운터)** 와 같은 발상.

**구현.** `mid`를 재부팅 시 리셋되는 SysTick에서 **영속 boot-epoch**로 교체:
- `seed = SHA-256(UID ‖ boot_epoch ‖ session_ctr)[0:4]`
- `boot_epoch` = 메타데이터의 영속 단조 `seq_counter`를 **부팅당 1회 +1**(`ota_meta_bump_seq`, ping-pong 원자커밋·전원단절 안전). 첫 RequestSeed에 lazy bump(메인루프 = flash 안전), persist-then-use로 power-loss에도 재사용 없음.
- `session_ctr` = 부팅 내 요청별 증가.
- → epoch가 재부팅에도 유지·증가 → seed 비반복 → 캡처 Key 무효. (NV 불가 시 `NRC 0x22`)

**검증.**
- 단위: `test_sec_freshness`(현행 재현성 + 수정 닫힘 실증 5종), `test_ota_meta`(bump 단조성·상태보존·실패 3종) — `ceedling` 0 실패.
- **on-target TC-ATK-005 PASS(2026-06-13, 양 ECU):** 캡처→리셋→재전송에서 `Seed2 ≠ Seed1` + 옛 Key `NRC 0x35` 거부, Unlock 정상(회귀 없음). 러너 `tools/seed_probe.py`.

**잔여(여전히 옵션 C 필요).**
- epoch가 **CRC-only 메타**라 물리(SWD) 공격자는 카운터를 되돌릴 수 있음 → **네트워크 replay만 차단, 물리 변조는 잔여.** 변조저항 단조 카운터는 ATECC608A(ADR-006).
- Seed **예측 가능성(낮은 엔트로피)** 은 그대로 — replay가 아닌 별도 속성이며 표준 DRBG는 옵션 B/C 후속.

---

## 7. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-02 | 최초 작성 — TRNG 부재로 SP 800-90 DRBG 미준수, SW nonce 잠정 채택 및 개선 경로(ADC/지터→HMAC_DRBG, HSM) 기록 |
| 1.1 | 2026-06-13 | **reboot-replay 해소(§6)** — SysTick→영속 boot-epoch(seq_counter bump). 단위 실증(취약 재현+수정) + on-target TC-ATK-005 PASS(양 ECU). 한계 #1·#3 종결, 물리 변조·엔트로피는 잔여(ADR-006) |
