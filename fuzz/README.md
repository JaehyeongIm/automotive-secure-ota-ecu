# fuzz/ — 호스트 커버리지 가이드 퍼징

OTA가 신뢰하지 않는 입력(공격자 통제 바이트)을 파싱하는 순수 로직(`ota_meta`·`uds`)을
ASAN/UBSAN과 함께 자동·대량 자극해 메모리 안전 결함을 발굴한다.
계획·결과·트리아지는 [FT-001](../docs/test/FT-001_Fuzz_Test_Plan_Report.md).

## 하니스

| 파일 | 대상 | 비고 |
|---|---|---|
| `harness_imghdr.c` | FH-1 이미지 헤더 파서 | `ota_img_header_read` |
| `harness_uds.c` | FH-3 UDS 0x36 경로 (다중블록 v2) | endless-data 누적·16KB 슬롯모델 |
| `harness_uds_block.c` | FH-3 단일블록 변이 | F-003 회귀·엔진 교차검증용 |

모든 하니스는 `LLVMFuzzerTestOneInput` ABI → **libFuzzer·AFL++ 공통**(컴파일러만 교체).

## libFuzzer (macOS, 기본)

```sh
./build.sh                    # Homebrew LLVM(-fsanitize=fuzzer,address,undefined) → bin/
./bin/harness_uds -max_len=600
```

Apple clang엔 libFuzzer가 없어 `brew install llvm` 필요.

## AFL++ (Docker/Linux)

동일 하니스를 `afl-clang-fast`로 재컴파일 → **엔진 이식성 실증 + F-003 교차검증**.

```sh
brew install colima docker && colima start   # 1회: Linux 컨테이너 런타임
./afl/run_afl.sh                              # 수정본 harness_uds 60s → 무크래시(방어 성립)
./afl/run_afl.sh --f003 120                   # 가드 임시해제 복사본+단일블록 → CWE-121 재발견
```

`--f003`은 **컨테이너 /tmp 복사본**의 per-block 가드만 비활성화한다(호스트 `uds.c` 불변).
결과: AFL++ havoc가 시드(250B)를 키워 >260B(예: 266B) 입력으로 `padded[260]` 오버플로를 독립 재발견 →
libFuzzer(<1s)와 다른 엔진·변이로 교차 확인. 가드 복원 시 양 엔진 무크래시.
