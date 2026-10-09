#!/usr/bin/env bash
# Promotes the live staging Remote Config to prod after you've reviewed the difference.
#   scripts/promote_remote_config.sh          shows the diff and asks before publishing
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP:?}"' EXIT
cd "$ROOT/firebase"
firebase remoteconfig:get --project staging -o "$TMP/staging.json"
firebase remoteconfig:get --project prod -o "$TMP/prod.json"
python3 - "$TMP/prod.json" "$TMP/staging.json" <<'PY'
import json, sys
a, b = (json.load(open(p)).get("parameters", {}) for p in sys.argv[1:3])
for k in sorted(set(a) | set(b)):
    va = a.get(k, {}).get("defaultValue", {}).get("value")
    vb = b.get(k, {}).get("defaultValue", {}).get("value")
    if va != vb: print(f"  {k}: prod {va} -> {vb}")
PY
read -r -p "Publish these values to prod? [y/N] " ok
[ "$ok" = "y" ] || { echo "nothing published"; exit 0; }
python3 - "$TMP/staging.json" "$TMP/publish.json" <<'PY'
import json, sys
t = json.load(open(sys.argv[1])); t.pop("version", None); t.pop("etag", None)
json.dump(t, open(sys.argv[2], "w"), indent=2)
PY
# deploy the staging template to prod through a throwaway config that points at it
cp "$TMP/publish.json" remoteconfig.promote.json
python3 -c "import json;c=json.load(open('firebase.json'));c['remoteconfig']={'template':'remoteconfig.promote.json'};json.dump(c,open('firebase.promote.json','w'))"
trap 'rm -rf "${TMP:?}" remoteconfig.promote.json firebase.promote.json' EXIT
firebase deploy --project prod --only remoteconfig --config firebase.promote.json
