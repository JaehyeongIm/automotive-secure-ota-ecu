#!/usr/bin/env bash
# Build firmware for the specified ECU and slot.
#
# Usage: ci/build.sh <drive|sensor> <A|B>
# Output: artifacts/<ecu>_slot<A|B>.bin
#
# CubeIDE-generated makefiles embed absolute linker-script paths.
# This script patches them to the current workspace before building.

set -euo pipefail

ECU=$1
SLOT=$2
REPO=$(realpath "$(dirname "$0")/..")

case $ECU in
  drive)  PROJ=DriveECU  ;;
  sensor) PROJ=SensorECU ;;
  *) echo "ERR: unknown ECU '$ECU'" >&2; exit 1 ;;
esac

case $SLOT in
  A) LD_NAME="STM32F446RETX_FLASH.ld"       ;;
  B) LD_NAME="STM32F446RETX_FLASH_SlotB.ld" ;;
  *) echo "ERR: unknown slot '$SLOT'" >&2; exit 1 ;;
esac

# DriveECU has a dedicated SlotB build dir; SensorECU reuses Debug with a patched linker.
if [ "$SLOT" = "B" ] && [ -d "$REPO/$PROJ/SlotB" ]; then
  BUILD_DIR="$REPO/$PROJ/SlotB"
else
  BUILD_DIR="$REPO/$PROJ/Debug"
fi

LD_SCRIPT="$REPO/$PROJ/$LD_NAME"

echo "[BUILD] ECU=$PROJ  SLOT=$SLOT  BUILD_DIR=$BUILD_DIR"
echo "[BUILD] Linker: $LD_SCRIPT"

# Patch the hardcoded absolute linker-script path in the CubeIDE makefile.
# The regex matches any absolute path to an STM32 .ld file (idempotent).
perl -i -pe \
  's|-T"[^"]*STM32F446RETX_FLASH[^"]*\.ld"|-T"'"$LD_SCRIPT"'"|g' \
  "$BUILD_DIR/makefile"

make -C "$BUILD_DIR" -j"$(nproc)" all

mkdir -p "$REPO/artifacts"
cp "$BUILD_DIR/$PROJ.bin" "$REPO/artifacts/${ECU}_slot${SLOT}.bin"
echo "[BUILD] Output: artifacts/${ECU}_slot${SLOT}.bin"
