#!/usr/bin/env bash
# 단일 보드 USB(ST-Link) 플래시 — 서명 앱(슬롯A v2) + CONFIRMED 메타를 굽는다.
# OTA(CAN) 불필요. hil_runner의 setup_bench에서 "보드별 플래시"만 추출한 것.
#
# 사용:
#   tools/flash.sh <drive|sensor> <st-link-serial> [--build] [--bl]
#     --build : 굽기 전 최신 소스로 재빌드+재서명(build_fixtures.sh) — ABOM 등 반영하려면 필수
#     --bl    : 부트로더도 함께 플래시(평소엔 불변이라 생략)
#   ST-Link 시리얼 확인:  st-info --probe
#
# 예) ABOM 새 펌웨어를 두 보드에 적용:
#   tools/flash.sh drive  0669FF485775495067211743 --build
#   tools/flash.sh sensor 066EFF485775495067194557 --build
set -euo pipefail

ECU=${1:-}
SERIAL=${2:-}
if [ -z "$ECU" ] || [ -z "$SERIAL" ]; then
  echo "사용: $0 <drive|sensor> <st-link-serial> [--build] [--bl]" >&2
  echo "      (시리얼: st-info --probe)" >&2
  exit 1
fi
shift 2

DO_BUILD=0; DO_BL=0
for a in "$@"; do
  case "$a" in
    --build) DO_BUILD=1 ;;
    --bl)    DO_BL=1 ;;
    *) echo "알 수 없는 옵션: $a" >&2; exit 1 ;;
  esac
done

case "$ECU" in
  drive|sensor) ;;
  *) echo "ERR: ecu는 drive|sensor 여야 함" >&2; exit 1 ;;
esac

REPO=$(realpath "$(dirname "$0")/..")
cd "$REPO"
PY=${PYTHON:-python3}
SF=(st-flash --serial "$SERIAL")
IMG="fixtures/${ECU}_v2_A.bin"

# st-flash 1회 재시도 래퍼 — ST-Link "0 KiB flash / Unknown memory region" 글리치 대응.
# 쓰기 사이엔 reset하지 않고(타깃 halt 유지) 마지막에만 reset → 도는 앱의 SWD 간섭 방지.
sf() { "${SF[@]}" "$@" || { echo "  ! st-flash 실패 → 1s 후 재시도"; sleep 1; "${SF[@]}" "$@"; }; }

if [ "$DO_BUILD" = 1 ]; then
  echo "== [재빌드+재서명] 최신 소스(ABOM 등) → $IMG =="
  KEY=${KEY:-ota-private-key.pem} bash tools/build_fixtures.sh "$ECU"
fi

if [ ! -f "$IMG" ]; then
  echo "ERR: $IMG 없음 — --build 로 먼저 빌드하거나 tools/build_fixtures.sh $ECU 실행" >&2
  exit 1
fi
[ "$DO_BUILD" = 1 ] || echo "주의: 기존 $IMG 를 굽습니다. 새 소스 변경을 반영하려면 --build 를 쓰세요."

if [ "$DO_BL" = 1 ]; then
  echo "== [부트로더] 빌드 + 플래시(0x08000000) =="
  make -C Bootloader/Debug -j4 all
  arm-none-eabi-objcopy -O binary Bootloader/Debug/Bootloader.elf /tmp/flash_bl.bin
  sf write /tmp/flash_bl.bin 0x08000000
fi

echo "== [앱+메타] $ECU 플래시 (ST-Link $SERIAL) =="
SZ=$("$PY" -c "import os,sys; print(os.path.getsize(sys.argv[1]))" "$IMG")   # 메타 a-size = 서명이미지 크기
META="/tmp/flash_meta_${ECU}.bin"
"$PY" tools/forge_meta.py --preset confirmed-a --a-version 2 --a-size "$SZ" --out "$META"
sf write "$IMG"  0x08010000      # 서명 앱 → 슬롯 A([헤더][코드][서명])  (halt 유지)
sf write "$META" 0x08008000      # 메타 A (CONFIRMED, a-size=$SZ)
sf write "$META" 0x0800C000      # 메타 B (이중화) — 둘 다 써야 옛 high-seq 사본을 덮음
"${SF[@]}" reset                 # 마지막에 한 번만 reset → 새 펌웨어 부팅

echo ""
echo "== 완료: $ECU 보드가 v2(슬롯A CONFIRMED)로 부팅. UART에서 부팅 로그 확인 =="
