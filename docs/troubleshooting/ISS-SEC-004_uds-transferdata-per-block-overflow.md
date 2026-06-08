# ISS-SEC-004: UDS TransferData per-block 스택 버퍼 오버플로 (CWE-121) — 퍼징 발견

> 8D 경량판. FH-3 커버리지 가이드 퍼징이 endless-data 수정이 놓친 신규 메모리 손상을 발견.

| 항목 | 내용 |
|---|---|
| 증상 | 데이터 **257B↑** 단일 TransferData(0x36) 블록 → `padded[260]` 스택 버퍼 오버플로 (ASAN: `stack-buffer-overflow @ uds.c:252`) |
| 상태 | ✅ 해결됨 |
| 심각도 | Medium (post-auth 필요 / 단 공격자 제어 스택 침범 → 잠재 DoS·코드실행) |
| 영향 | SensorECU·DriveECU `uds.c` 0x36 핸들러 |
| 관련 | SR-ATK-007 · [FT-001 F-003](../test/FT-001_Fuzz_Test_Plan_Report.md) · CWE-121 · oracle=ASAN(ISO/SAE 21434 §10.4) · TARA 위협 등재 검토 |

---

## D2. 문제 정의 (Is–Is Not)

- **무엇이:** 0x36 핸들러가 `chunk_len = len-2`를 고정 버퍼 `padded[260]`/광고 maxBlockLen(256)에 대해 **검증하지 않고** `memset(padded,0xFF,write_len)`·`memcpy` 수행.
- **Is:** 단일 블록 데이터 ≥257B (chunk_len 261 → write_len 264 > 260). **Is Not:** ≤256B(정상 클라이언트는 광고된 한도 준수) → 그래서 정상 OTA·기존 단위테스트(2~8B 블록)는 미발생.
- **재현:** `fuzz/bin/harness_uds`(FH-3) <1초, 또는 261B 단일 0x36 블록 직접 주입.

## D4. 근본 원인 분석 (5 Whys)

1. 왜 오버플로? → write_len(264) > `padded`(260) → 4B 초과 쓰기.
2. 왜 write_len이 큼? → chunk_len(261)을 그대로 4B 정렬 → 264.
3. 왜 chunk_len이 261? → `uds_on_isotp_rx`가 512B까지 수용, 블록 크기 상한 검사 없음.
4. 왜 검사 없나? → 0x34에서 maxBlockLen=258(데이터 256)을 *광고*만 하고 0x36에서 **집행 안 함**. endless-data 가드(FR-CAN-012)는 *누적 vs 선언*만 봄 → *블록 vs 버퍼* 사각지대.
5. 왜 여태 안 드러났나? → 정상 클라이언트·테스트가 광고 한도를 지켜 큰 블록을 안 보냄 → **퍼저가 적대적 입력으로 발견**.

→ **근본 원인:** 수신측이 ISO 14229 maxNumberOfBlockLength를 **집행하지 않음**(per-block 경계 미검증).

## D5–D6. 영구 시정 & 검증

- **수정(양 ECU):** `chunk_len` 직후 가드 추가 —
  ```c
  if (chunk_len > 256) { nrc(sid, 0x31); break; }   /* 광고 maxBlockLen 집행 → padded 보호 */
  ```
- **검증:** ① 원 PoC(261B) 재생 → **클린**(NRC 0x31) ② 수정 후 **재퍼징 1.4M회 무크래시**(동일 퍼저가 더는 미발견) ③ `test_uds_state` **30/30 PASS**(신규 `test_transfer_data_oversized_block` 포함, 회귀 0).

## D7. 재발 방지 (Lessons Learned)

- **sanitizer 퍼징 상시화:** ASAN 빌드 퍼징을 CI에 → 동종 경계 결함 조기 검출.
- **체크리스트:** "외부 입력 길이는 사용 전 *버퍼·프로토콜 상한*에 검증" / 광고한 프로토콜 한도는 *집행*까지.
- **교훈:** *광고(advertise) ≠ 집행(enforce).* 광고한 maxBlockLen을 코드가 강제하지 않으면 공격자는 무시한다. **누적 가드와 per-block 가드는 별개의 방어**임.
- **후속:** TARA에 "OTA TransferData 메모리 손상" 위협 등재 검토(T-1 코드실행 경로 보강).
