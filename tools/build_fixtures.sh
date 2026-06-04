#!/usr/bin/env bash
# HIL 픽스처 일괄 빌드 — 정상 v1/v2/v3 + 고장앱을 한 번에 빌드·서명.
#
# 사용:  bash tools/build_fixtures.sh [drive|sensor]
# 출력:  fixtures/<ecu>_v2_A.bin  (베이스라인)
#        fixtures/<ecu>_v1_B.bin  (TC-01 anti-rollback)
#        fixtures/<ecu>_v3_B.bin  (TC-01b 상위 허용)
#        fixtures/<ecu>_v3broken_B.bin (TC-02 3-strike, HIL_SELFTEST_FAIL)
#
# anti-rollback 버전은 sign_firmware --version 값(헤더)이라 같은 앱을 버전만 달리 서명한다.
set -euo pipefail

ECU=${1:-drive}
KEY=${KEY:-ota-private-key.pem}
REPO=$(realpath "$(dirname "$0")/..")
cd "$REPO"

case "$ECU" in
  drive)  PROJ=DriveECU;  EID=1 ;;
  sensor) PROJ=SensorECU; EID=2 ;;
  *) echo "ERR: ecu must be drive|sensor" >&2; exit 1 ;;
esac
[ -f "$KEY" ] || { echo "ERR: 서명 키 없음: $KEY (KEY=... 로 지정)" >&2; exit 1; }

MAIN="$PROJ/Core/Src/main.c"
mkdir -p fixtures

sign() {  # sign <slot> <version> <outname>
  python tools/sign_firmware.py "artifacts/${ECU}_slot$1.bin" "$KEY" \
    --version "$2" --ecu-id "$EID" --out "fixtures/$3"
}

echo "== [1/3] 정상 이미지 빌드/서명 =="
bash ci/build.sh "$ECU" A >/dev/null
sign A 2 "${ECU}_v2_A.bin"
bash ci/build.sh "$ECU" B >/dev/null
sign B 1 "${ECU}_v1_B.bin"
sign B 3 "${ECU}_v3_B.bin"

echo "== [2/3] 고장 앱 빌드(HIL_SELFTEST_FAIL) — 임시 수정 후 원복 =="
BAK="/tmp/${PROJ}_main.bak"
cp "$MAIN" "$BAK"
trap 'cp "$BAK" "$MAIN"; echo "[restore] $MAIN 원복"' EXIT
{ echo '#define HIL_SELFTEST_FAIL 1'; cat "$BAK"; } > "$MAIN"
bash ci/build.sh "$ECU" B >/dev/null
sign B 3 "${ECU}_v3broken_B.bin"
cp "$BAK" "$MAIN"; trap - EXIT
echo "[restore] $MAIN 원복"

echo ""
echo "== [3/3] 완료 =="
ls -l fixtures/${ECU}_*.bin
