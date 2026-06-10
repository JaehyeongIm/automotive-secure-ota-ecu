# AFL++ 퍼징 셋업·실행 작업기록 (FT-001 부속)

> [FT-001](FT-001_Fuzz_Test_Plan_Report.md)의 §2·§6에 기록된 **AFL++ 도입**을, 진행 순서와 *각 단계의 이유*까지 따라 읽을 수 있게 풀어 쓴 작업기록.
> 결론만 보려면 FT-001 §6 캠페인 로그, 재현만 하려면 [`fuzz/README.md`](../../fuzz/README.md) 참고.

---

## 0. 목표 — 왜 AFL++를 추가했나

이미 **libFuzzer**로 UDS 0x36 경로를 퍼징해 F-003(CWE-121, `padded[260]` 스택 오버플로)을 발견·수정했다. 여기에 **AFL++**를 더한 목적은 둘:

1. **엔진 이식성 실증** — 우리 하니스는 `LLVMFuzzerTestOneInput`라는 *표준 진입점*으로 작성돼 있다. 이게 사실이라면 libFuzzer뿐 아니라 AFL++로도 *코드 수정 없이* 컴파일러만 바꿔 돌아가야 한다. 실제로 그런지 확인.
2. **교차검증** — 서로 다른 변이(mutation) 엔진이 *독립적으로* 같은 결함을 찾으면, 그 결함이 "한 도구의 산물"이 아니라 진짜임을 보강한다. OSS-Fuzz가 한 타깃에 libFuzzer·AFL++·Honggfuzz를 동시에 돌리는 것과 같은 이유.

---

## 1. 개념 — libFuzzer vs AFL++

| | libFuzzer | AFL++ |
|---|---|---|
| 실행 모델 | **in-process**(같은 프로세스에서 함수 반복 호출) | fork-server / persistent |
| 설치 | `-fsanitize=fuzzer` 플래그 하나 | `afl-clang-fast`로 컴파일 + `afl-fuzz` |
| 진입점(ABI) | `LLVMFuzzerTestOneInput(data,size)` | **동일** (드라이버 `libAFLDriver.a`가 `main` 제공) |
| 유지보수 | 유지보수 모드 | 활발(커뮤니티 표준) |

핵심: **둘 다 `LLVMFuzzerTestOneInput`을 받는다.** 그래서 하니스 한 벌로 엔진만 바꿔 돌릴 수 있다 — 이게 "엔진 lock-in 없음"의 실체.

---

## 2. 환경 구축 — macOS에 Linux 컨테이너

**왜 Docker/Linux?** AFL++는 Linux 우선 도구다. macOS 네이티브 빌드는 fork 성능·일부 기능에 제약이 있어, 깨끗한 Linux 컨테이너에서 돌리는 게 정석(실무 환경 재현이기도 함). 이 맥엔 컨테이너 런타임이 없어 새로 깔았다.

**colima란?** Docker Desktop 대신 쓰는 **경량 CLI 전용 컨테이너 런타임**(내부적으로 Lima라는 작은 Linux VM을 띄움). 무료·라이선스 부담 없음.

```sh
brew install colima docker     # colima(런타임) + docker(CLI 클라이언트)
colima start --cpu 4 --memory 4   # Linux VM 기동 (Ubuntu 24.04, 4 CPU, 4GB)
docker info                       # 데몬 연결 확인
```

**AFL++ 이미지 받기 + 확인:**

```sh
docker pull aflplusplus/aflplusplus:latest
docker run --rm aflplusplus/aflplusplus uname -m     # aarch64 = arm64 네이티브(에뮬레이션 없음 → 빠름)
```

확인 결과: `afl-clang-fast`·`afl-fuzz`가 `/usr/local/bin`에, libFuzzer 호환 드라이버 `libAFLDriver.a`가 `/AFLplusplus/`에 있음.

---

## 3. 빌드 원리 — 동일 하니스, 컴파일러만 교체

libFuzzer 빌드(`fuzz/build.sh`)는 `clang -fsanitize=fuzzer,address,undefined ...`였다. AFL++는:

```sh
afl-clang-fast -O1 -g -fsanitize=address,undefined -DUNIT_TEST \
  -I.../Core/Inc -I.../test/support \
  fuzz/harness_uds.c  uds.c ota_meta.c sha256.c hmac_sha256.c hal_stubs.c \
  /AFLplusplus/libAFLDriver.a  -o harness_uds_afl
```

차이는 두 가지뿐:
- `clang` → **`afl-clang-fast`** (커버리지 계측을 AFL++ 방식으로 삽입)
- `-fsanitize=fuzzer`(libFuzzer가 `main` 제공) → **`libAFLDriver.a`**(AFL++용 `main` 제공, 내부에서 `LLVMFuzzerTestOneInput`을 반복 호출)

하니스 `.c`와 대상 소스(`uds.c` 등)는 **그대로**다.

---

## 4. Step A — 수정본에서 AFL++ 실행 (방어 성립 확인)

현재(=F-003 수정된) `harness_uds.c`(v2 다중블록)를 AFL++로 60초 돌렸다.

```sh
afl-fuzz -i seeds -o out -m none -V 60 -- ./harness_uds_afl
```
- `-m none`: ASAN은 가상메모리를 크게 쓰므로 메모리 제한 해제(안 하면 ASAN 빌드가 죽음).
- `-V 60`: 60초만 돌고 종료.

**결과:** `19 new corpus items, 13.94% coverage, 0 crashes`.
→ **의미:** AFL++가 우리 하니스를 *실제로* 돌렸고(이식성 ✓), 수정된 방어가 *2번째 엔진에서도* 무너지지 않음(크래시 0).

---

## 5. Step B — F-003 교차검증 (CWE-121 독립 재발견)

여기가 핵심. "AFL++도 같은 결함을 찾는가?"를 보려면 두 가지가 필요했다.

**(1) 왜 단일블록 하니스(`harness_uds_block.c`)를 새로 만들었나**
기존 v2 하니스는 입력을 *다중 블록*으로 쪼개 주입하는데, 블록당 길이가 1바이트(≤255B)라 `padded[260]` 경계(261B 이상)에 **닿을 수가 없다**(v2는 endless-data *누적* 경로용). 그래서 퍼즈 입력 전체를 *한 블록*으로 주입하는 단일블록 하니스를 만들어 padded 경로를 직접 때렸다.

**(2) 왜 가드를 임시로 껐나**
지금 `uds.c`엔 `if (chunk_len > 256) NRC` 가드가 있어 큰 블록을 막는다(= 수정 완료). 그래서 수정본 그대로면 크래시가 *안* 난다(당연—고쳤으니까). "**가드가 없었다면 퍼저가 이걸 잡아내는가**"를 보이려고, 가드를 한 줄 비활성화한 상태로 퍼징했다.

> 중요: 가드 해제는 **컨테이너 안 `/tmp` 복사본에서만** 했다(`sed`로 `chunk_len > 256` → `0 && chunk_len > 256`). 호스트의 `uds.c` 원본은 **건드리지 않았다**(양 ECU 가드 그대로 확인).

**실행:**
```sh
cp uds.c /tmp/uds.c && sed -i 's/chunk_len > 256/0 && chunk_len > 256/' /tmp/uds.c   # 복사본만 해제
afl-clang-fast ... harness_uds_block.c /tmp/uds.c ... -o /tmp/h
head -c 250 /dev/zero > seeds/s          # 시드 250B (임계 261 미만 → 안전, AFL이 키우게 함)
afl-fuzz -i seeds -o out -m none -V 120 -- /tmp/h
```

**결과:** AFL++ **havoc** 변이가 250B 시드를 키워 **>260B(266~276B)** 입력을 만들자 →
```
AddressSanitizer: stack-buffer-overflow ... WRITE of size 268 ... in __asan_memset
```
= libFuzzer가 찾았던 것과 **동일한** `padded[260]` 오버플로(CWE-121)를, *다른 엔진·다른 변이전략*으로 독립 재발견. 가드를 복원하면 양 엔진 모두 무크래시.

---

## 6. 정정 — 발견 시간 단위 (ms)

처음 결과 보고에서 "39초 만에 발견"이라 했는데 **틀렸다.** AFL++ 크래시 파일명의 `time` 필드는 **밀리초(ms)** 단위다.
- 1차 실행: `time:39` = **39ms**
- 커밋본 검증: `time:1937` = **1.9초**

둘 다 **2초 내**(run마다 변동). 문서엔 "havoc로 2초 내"로 정확히 기재했다. (방어 불가능한 숫자를 남기지 않기 위해 ms/초를 정정.)

---

## 7. 만든 산출물

| 파일 | 내용 |
|---|---|
| `fuzz/harness_uds_block.c` | FH-3 단일블록 하니스(교차검증·회귀용) |
| `fuzz/afl/run_afl.sh` | Docker로 AFL++ 빌드·실행 재현 러너(`default` / `--f003`) |
| `fuzz/README.md` | libFuzzer·AFL++ 사용법 |
| `FT-001` §2·§6·§7·§8·§9 | AFL++ 도입·교차검증 결과 반영 |

---

## 8. 재현 방법

```sh
brew install colima docker && colima start     # 1회
./fuzz/afl/run_afl.sh                           # 수정본 60s → 무크래시(방어 성립)
./fuzz/afl/run_afl.sh --f003 120                # 가드해제 복사본 → CWE-121 재발견
```

## 9. 환경 정리 (선택)

```sh
colima stop          # VM 일시정지(다음에 colima start로 재개)
colima delete        # VM 완전 삭제
docker image rm aflplusplus/aflplusplus:latest
```
