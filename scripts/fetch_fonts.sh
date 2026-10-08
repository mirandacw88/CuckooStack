#!/usr/bin/env bash
# Downloads the web build's fonts from github.com/google/fonts (SIL OFL 1.1), pins the CSS weights and
# subsets them to the glyphs the game draws, writing assets/fonts/*.ttf + licences.
#   Orbitron 900            -> --display (score, title, stats, popups), shipped renamed as "Cuckoo Display"
#   Chakra Petch 500 / 700  -> --body (paragraphs; tags, labels, pills, gate label)
#   Noto Sans JP 700        -> katakana / kanji on the neon signs (the web build falls back to a system CJK font)
# Requires: python3 -m pip install fonttools
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/assets/fonts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PY="${PYTHON:-python3}"
BASE=https://raw.githubusercontent.com/google/fonts/main/ofl
get() { curl -sfL -o "$TMP/$2" "$BASE/$1"; }
get "orbitron/Orbitron%5Bwght%5D.ttf" orbitron.ttf
get "chakrapetch/ChakraPetch-Medium.ttf" chakra500.ttf
get "chakrapetch/ChakraPetch-Bold.ttf" chakra700.ttf
get "notosansjp/NotoSansJP%5Bwght%5D.ttf" notojp.ttf
for f in orbitron chakrapetch notosansjp; do get "$f/OFL.txt" "$f-OFL.txt"; cp "$TMP/$f-OFL.txt" "$OUT/OFL-$f.txt"; done

LATIN="U+0020-007E,U+00B7,U+00D7,U+2013,U+2014,U+2019,U+2026"
JP_TEXT="コケコッコーニワトリタマゴネオン卵屋養鶏場"
"$PY" -m fontTools.varLib.instancer "$TMP/orbitron.ttf" wght=900 -q -o "$TMP/orbitron900.ttf"
"$PY" -m fontTools.varLib.instancer "$TMP/notojp.ttf" wght=700 -q -o "$TMP/notojp700.ttf"
sub() { "$PY" -m fontTools.subset "$1" --output-file="$2" $3 --layout-features='kern' --no-hinting --desubroutinize; }
sub "$TMP/orbitron900.ttf" "$TMP/display.ttf" "--unicodes=$LATIN"
# Orbitron is licensed with Reserved Font Name "Orbitron" (OFL section 3): this instanced + subset copy is a
# Modified Version, so it must ship under a different name.
"$PY" - "$TMP/display.ttf" "$OUT/CuckooDisplay-Black.ttf" <<'PYEOF'
import sys
from fontTools.ttLib import TTFont
f = TTFont(sys.argv[1])
names = {1: "Cuckoo Display", 2: "Regular", 3: "CuckooDisplay-Black;derived-from-Orbitron", 4: "Cuckoo Display Black",
         6: "CuckooDisplay-Black", 16: "Cuckoo Display", 17: "Black"}
for rec in list(f["name"].names):
    if rec.nameID in names or rec.nameID in (21, 22, 25):
        f["name"].removeNames(nameID=rec.nameID)
for nid, val in names.items():
    f["name"].setName(val, nid, 3, 1, 0x409)
f.save(sys.argv[2])
PYEOF
sub "$TMP/chakra500.ttf" "$OUT/ChakraPetch-Medium.ttf" "--unicodes=$LATIN"
sub "$TMP/chakra700.ttf" "$OUT/ChakraPetch-Bold.ttf" "--unicodes=$LATIN"
sub "$TMP/notojp700.ttf" "$OUT/NotoSansJP-Bold-signs.ttf" "--text=$JP_TEXT"
ls -la "$OUT"
