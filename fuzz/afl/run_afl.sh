#!/usr/bin/env bash
# AFL++ 퍼징 러너 (Docker/Linux) — libFuzzer와 *동일 하니스*(LLVMFuzzerTestOneInput)를
# afl-clang-fast로 재컴파일해 엔진 이식성을 실증하고, F-003(CWE-121)을 교차검증한다.
#
# 사전: 컨테이너 런타임.  macOS:  brew install colima docker && colima start
# 사용:
#   fuzz/afl/run_afl.sh              # 현재(수정본) harness_uds 60s → 방어 성립(무크래시) 확인
#   fuzz/afl/run_afl.sh --f003 120   # per-block 가드 임시해제 복사본 + 단일블록 → CWE-121 재발견
#
# 주의: --f003 은 *컨테이너 안 /tmp 복사본*의 가드만 비활성화한다(호스트 uds.c 불변).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
IMG="${AFL_IMAGE:-aflplusplus/aflplusplus:latest}"
MODE="${1:-default}"
SECS="${2:-60}"
echo "[run_afl] image=$IMG mode=$MODE secs=${SECS}s repo=$REPO"

docker run --rm -i -e MODE="$MODE" -e SECS="$SECS" -v "$REPO":/src "$IMG" bash -s <<'INNER'
set -eu
INC="-I/src/SensorECU/Core/Inc -I/src/test/support"
CORE="/src/SensorECU/Core/Src/ota_meta.c /src/SensorECU/Core/Src/sha256.c /src/SensorECU/Core/Src/hmac_sha256.c /src/test/support/hal_stubs.c"
CFLAGS="-O1 -g -fsanitize=address,undefined -DUNIT_TEST"
DRIVER=/AFLplusplus/libAFLDriver.a
mkdir -p /tmp/seeds /tmp/out
export AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:symbolize=0

if [ "$MODE" = "--f003" ]; then
  echo "[f003] per-block 가드 임시 해제(복사본) + 단일블록 하니스 — 교차검증"
  cp /src/SensorECU/Core/Src/uds.c /tmp/uds.c
  sed -i 's/chunk_len > 256/0 \&\& chunk_len > 256/' /tmp/uds.c
  echo -n "  가드: "; grep -n 'chunk_len > 256' /tmp/uds.c
  # shellcheck disable=SC2086
  afl-clang-fast $CFLAGS $INC /src/fuzz/harness_uds_block.c /tmp/uds.c $CORE $DRIVER -o /tmp/h
  head -c 250 /dev/zero > /tmp/seeds/s          # 임계(261) 미만 시드 → AFL이 키워서 발견
else
  echo "[default] 현재(수정본) harness_uds — 방어 성립(무크래시) 확인"
  # shellcheck disable=SC2086
  afl-clang-fast $CFLAGS $INC /src/fuzz/harness_uds.c /src/SensorECU/Core/Src/uds.c $CORE $DRIVER -o /tmp/h
  printf '\x04\x04\x04\x04' > /tmp/seeds/s
fi

afl-fuzz -i /tmp/seeds -o /tmp/out -m none -V "$SECS" -- /tmp/h 2>&1 | tail -6 || true

CR="$(ls /tmp/out/default/crashes/id* 2>/dev/null | head -1 || true)"
if [ -n "$CR" ]; then
  echo "크래시: $(basename "$CR") — $(wc -c < "$CR")B"
  ASAN_OPTIONS=symbolize=1 /tmp/h "$CR" 2>&1 | grep -iE 'ERROR:|stack-buffer-overflow|WRITE of size|SUMMARY' | head -6 || true
else
  echo "크래시 없음 (default 모드면 방어 성립)"
fi
INNER
