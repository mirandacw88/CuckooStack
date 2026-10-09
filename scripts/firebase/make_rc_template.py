#!/usr/bin/env python3
"""Remote Config template from src/core/Tuning.h: one NUMBER parameter per tunable, default = the compiled-in value.

    python3 scripts/firebase/make_rc_template.py      -> firebase/remoteconfig.template.json

Every field in Tuning.h carries its Remote Config key in a trailing comment ("key"), so the template never drifts from
the game. Deploy with scripts/firebase_deploy.sh (staging), then promote to prod (scripts/promote_remote_config.sh).
"""
import json
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
src = open(os.path.join(ROOT, "src", "core", "Tuning.h")).read()
params = {}
for line in src.splitlines():
    m = re.match(r'\s*(?:int|float)\s+(\w+)(\[\d+\])?\s*=\s*([^;]+);\s*//\s*"([^"]+)"(?:\.\."([^"]+)")?\s*(.*)', line)
    if not m:
        continue
    name, arr, value, key, key_last, desc = m.groups()
    if arr:  # dailyDrop[7] = {20, 30, ...}; "drop_1".."drop_7"
        values = [v.strip() for v in value.strip("{} ").split(",")]
        prefix = key.rsplit("_", 1)[0]
        for i, v in enumerate(values):
            params[f"{prefix}_{i + 1}"] = {"defaultValue": {"value": v}, "description": f"{desc.strip()} (day {i + 1})", "valueType": "NUMBER"}
        continue
    params[key] = {"defaultValue": {"value": value.strip().rstrip("f")}, "description": desc.strip() or name, "valueType": "NUMBER"}

out = {"conditions": [], "parameters": dict(sorted(params.items()))}
path = os.path.join(ROOT, "firebase", "remoteconfig.template.json")
json.dump(out, open(path, "w"), indent=2)
print(f"wrote {path} ({len(params)} parameters)")
