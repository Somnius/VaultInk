#!/usr/bin/env bash
# Build the X4 Pro release firmware on this machine and publish the web installer
# (site/ plus the full image and manifest) to the gh-pages branch. GitHub only serves the files.
#
#   scripts/deploy_web_installer.sh            # build, then publish
#   PIO=/path/to/pio scripts/deploy_web_installer.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIO="${PIO:-pio}"
REMOTE="${REMOTE:-https://github.com/Somnius/VaultInk.git}"
cd "$ROOT"

if [[ -n "$(git status --porcelain -- src lib platformio.ini partitions_vaultink.csv)" ]]; then
  echo "Uncommitted firmware changes; commit them first so the installer matches a commit." >&2
  exit 1
fi

taskset -c 0-23 nice -n 10 "$PIO" run -e x4pro-gh_release

VERSION=$(grep -m1 '^version' platformio.ini | sed 's/.*= *//')
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cp site/* "$OUT/"
cp .pio/build/x4pro-gh_release/firmware.factory.bin "$OUT/vaultink-x4pro-factory.bin"
cat > "$OUT/manifest.json" <<EOF
{
  "name": "VaultInk",
  "version": "${VERSION}",
  "built": "$(date +%Y-%m-%d)",
  "commit": "$(git rev-parse --short HEAD)",
  "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32-S3", "parts": [ { "path": "vaultink-x4pro-factory.bin", "offset": 0 } ] }
  ]
}
EOF
touch "$OUT/.nojekyll"

cd "$OUT"
git init -q -b gh-pages
git add -A
git commit -q -m "Web installer ${VERSION} ($(git -C "$ROOT" rev-parse --short HEAD))"
git push -q -f "$REMOTE" gh-pages
echo "Published web installer ${VERSION}: https://somnius.github.io/VaultInk/"
sha256sum vaultink-x4pro-factory.bin
