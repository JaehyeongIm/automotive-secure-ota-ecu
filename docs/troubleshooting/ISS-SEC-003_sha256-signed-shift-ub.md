# ISS-SEC-003: sha256.c 부호 있는 좌측 시프트 오버플로 (UB) — UBSAN 검출

> 8D 경량판. 퍼징 하니스(FH-3) sanity 단계에서 UBSAN이 잠복 UB를 표면화한 사례.

| 항목 | 내용 |
|---|---|
| 증상 | `UBSan: left shift of 165 by 24 places cannot be represented in type 'int'` @ `sha256.c:49` |
| 상태 | ✅ 해결됨 |
| 심각도 | Low (타깃 동작은 정상 / 단 UB·MISRA 위반) |
| 영향 | SHA-256 3개 사본(SensorECU·Bootloader·DriveECU) — HMAC SecurityAccess(FR-CAN-010)·펌웨어 해싱 등 SHA 경로 전반 |
| 관련 | FR-CAN-010 · [FT-001 F-002](../test/FT-001_Fuzz_Test_Plan_Report.md) · oracle=UBSAN(ISO/SAE 21434 §10.4) |

---

## D2. 문제 정의 (Is–Is Not)

- **무엇이:** 메시지 워드 조립 `m[i] = (data[j] << 24) | …` 에서 `data[j]`(`unsigned char`)가 시프트 전 `int`로 정수 승격된 뒤 `<< 24` 됨.
- **Is:** 입력 바이트가 `0x80` 이상일 때(예: SecurityAccess seed `0xA5A5A5A5`의 `0xA5`). **Is Not:** 모든 바이트 `< 0x80`(예: ASCII PSK만 처리하면 미발생) → 그래서 평소 단위테스트(test_hmac 정답벡터)는 통과하면서도 숨어 있었다.
- **재현:** `fuzz/bin/harness_uds_sanity` 실행 → 프라이밍의 HMAC-SHA256 계산 중 UBSAN 출력.

## D4. 근본 원인 분석 (5 Whys)

1. 왜 UB? → `0xA5 << 24` = `0xA5000000`(2,768,240,640)가 `int` 최대(2,147,483,647) 초과 → 부호비트 침범 → **부호 있는 정수 오버플로(UB)**.
2. 왜 `int`로? → `unsigned char`가 시프트 연산 전 **정수 승격**으로 `int`(부호 있음)가 됨.
3. 왜 캐스트가 없었나? → 널리 쓰는 공개구현(Brad Conte SHA-256)이 시프트 전 unsigned 캐스트를 생략.
4. 왜 여태 안 드러났나? → 실 컴파일러/ARM은 의도대로 동작 + 정답벡터(<0x80 위주) 통과 → **정상 테스트로는 비가시**.
5. 왜 지금? → **sanitizer(UBSAN) 빌드**로 퍼징하니 첫 sanity에서 즉시 표면화.

→ **근본 원인:** 바이트→워드 조립 시 시프트 전 부호 없는 타입 캐스트 누락(CWE-190 계열).

## D5–D6. 영구 시정 & 검증

- **수정:** 4개 항 모두 `(WORD)`(=`unsigned int`) 캐스트 → 부호 없는(정의된) 시프트.
  ```c
  m[i] = ((WORD)data[j] << 24) | ((WORD)data[j+1] << 16)
       | ((WORD)data[j+2] << 8) |  (WORD)data[j+3];
  ```
  **3개 사본 모두**(SensorECU/Core/Src, Bootloader/Core/Lib, DriveECU/Core/Src) 동일 적용.
- **검증:** ① UBSAN 재실행 클린(sanity) ② `test_hmac` RFC 4231 **3/3 PASS**(값 불변 입증) ③ `test_uds_state` **29/29 PASS**(HMAC unlock 회귀 없음).

## D7. 재발 방지 (Lessons Learned)

- **sanitizer 상시화:** ASAN/UBSAN 빌드를 퍼징/CI에 상시 적용(FT-001) → 동종 UB 조기 검출.
- **정적분석 룰:** "시프트 전 부호 없는 캐스트" / 부호 있는 시프트 금지(MISRA-C 계열) 룰로 클래스 차단.
- **교훈:** *정상 테스트(정답벡터) 통과 ≠ UB 없음.* 외부 공개 crypto 도입 시 **sanitizer 통과**를 수용 체크리스트에 포함.
