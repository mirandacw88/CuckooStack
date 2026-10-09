#!/usr/bin/env bash
# Deploys the Firebase backend (firebase/) to one environment: Cloud Functions, Firestore rules + indexes, Hosting
# (with the app-link files for that environment's app ID) and the Remote Config template (staging only: prod gets
# its template through scripts/promote_remote_config.sh after review).
#   scripts/firebase_deploy.sh staging
#   scripts/firebase_deploy.sh prod        (only from a clean git tree on main)
# Needs the Firebase CLI (npm i -g firebase-tools) logged in, and config/firebase/<env>/links.env with
# APPLE_TEAM_ID and ANDROID_SHA256 (the signing certificate fingerprint of that environment's Android app).
set -euo pipefail
ENV="${1:?usage: firebase_deploy.sh staging|prod}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
case "$ENV" in
  staging) APP_ID=com.cuckoostack.aerospheregames.staging ;;
  prod)
    APP_ID=com.cuckoostack.aerospheregames
    [ "$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)" = "main" ] || { echo "prod deploys only from main"; exit 1; }
    [ -z "$(git -C "$ROOT" status --porcelain)" ] || { echo "prod deploys need a clean git tree"; exit 1; }
    ;;
  *) echo "environment must be staging or prod"; exit 1 ;;
esac
LINKS="$ROOT/config/firebase/$ENV/links.env"
[ -f "$LINKS" ] || { echo "missing $LINKS (APPLE_TEAM_ID=..., ANDROID_SHA256=...)"; exit 1; }
set -a; . "$LINKS"; set +a
export APP_ID
mkdir -p "$ROOT/firebase/hosting/public/.well-known"
for f in apple-app-site-association assetlinks.json; do
  envsubst < "$ROOT/firebase/hosting/templates/$f" > "$ROOT/firebase/hosting/public/.well-known/$f"
done
python3 "$ROOT/scripts/firebase/make_rc_template.py"
ONLY="functions,firestore,hosting"
[ "$ENV" = "staging" ] && ONLY="$ONLY,remoteconfig"
(cd "$ROOT/firebase" && firebase deploy --project "$ENV" --only "$ONLY")
