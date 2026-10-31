#!/usr/bin/env bash
# Republish the web installer page (site/) to gh-pages without a new release: the firmware image and
# manifest stay those of the current version's release (downloaded from it, not rebuilt).
#
#   scripts/deploy_site_only.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REMOTE="${REMOTE:-https://github.com/Somnius/VaultInk.git}"
cd "$ROOT"

VERSION=$(grep -m1 '^version' platformio.ini | sed 's/.*= *//')
if ! gh release view "$VERSION" >/dev/null 2>&1; then
  echo "No release $VERSION on GitHub; use deploy_web_installer.sh." >&2
  exit 1
fi
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cp site/* "$OUT/"
gh release download "$VERSION" -p "vaultink-${VERSION}-x4pro-factory.bin" -D "$OUT"
mv "$OUT/vaultink-${VERSION}-x4pro-factory.bin" "$OUT/vaultink-x4pro-factory.bin"
cat > "$OUT/manifest.json" <<EOT
{
  "name": "VaultInk",
  "version": "${VERSION}",
  "built": "$(git log -1 --format=%cs "$VERSION")",
  "commit": "$(git rev-parse --short "$VERSION^{commit}")",
  "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32-S3", "parts": [ { "path": "vaultink-x4pro-factory.bin", "offset": 0 } ] }
  ]
}
EOT
touch "$OUT/.nojekyll"
cd "$OUT"
git init -q -b gh-pages
git add -A
git commit -q -m "Web installer ${VERSION}, page updated ($(git -C "$ROOT" rev-parse --short HEAD))"
git push -q -f "$REMOTE" gh-pages
echo "Published the page for ${VERSION}: https://somnius.github.io/VaultInk/"
