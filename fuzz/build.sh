#!/usr/bin/env bash
# Secure OTA 호스트 퍼징 빌드 — Homebrew LLVM의 libFuzzer + ASAN + UBSAN.
# (Apple clang엔 libFuzzer가 빠져 있어 Homebrew llvm을 쓴다.)
set -euo pipefail

LLVM="$(brew --prefix llvm)"
CC="$LLVM/bin/clang"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/SensorECU/Core/Src"
INC="$ROOT/SensorECU/Core/Inc"
SUPPORT="$ROOT/test/support"     # 호스트용 HAL 스텁/가짜 헤더 (Ceedling과 공용)
OUT="$ROOT/fuzz/bin"
mkdir -p "$OUT"

SAN="address,undefined"          # ASAN(메모리) + UBSAN(정의되지 않은 동작)
FLAGS="-g -O1 -fsanitize=fuzzer,$SAN -DUNIT_TEST -I$INC -I$SUPPORT"

build() {  # $1 = 하니스 이름, $2.. = 함께 컴파일할 소스
  local name="$1"; shift
  echo "[build] $name (fuzz)"
  # shellcheck disable=SC2086
  "$CC" $FLAGS "$ROOT/fuzz/$name.c" "$@" -o "$OUT/$name"
}

# ── FH-1: 헤더 파서 ──
build harness_imghdr "$SRC/ota_meta.c"

# ── FH-3: UDS 0x36 경로 ──  (uds.c 대상 + 의존 소스 함께)
UDS_SRCS="$SRC/uds.c $SRC/ota_meta.c $SRC/sha256.c $SRC/hmac_sha256.c $SUPPORT/hal_stubs.c"

# (1단계) sanity — 퍼저 엔진 없이 일반 프로그램(main). uds.c 로그가 보이게 한다.
echo "[build] harness_uds_sanity"
# shellcheck disable=SC2086
"$CC" -g -O1 -fsanitize=$SAN -DUNIT_TEST -DHARNESS_SANITY -I"$INC" -I"$SUPPORT" \
  "$ROOT/fuzz/harness_uds.c" $UDS_SRCS -o "$OUT/harness_uds_sanity"

# (2단계) fuzz
# shellcheck disable=SC2086
build harness_uds $UDS_SRCS

echo "OK -> $OUT"
