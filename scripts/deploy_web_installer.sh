#!/usr/bin/env bash
# Build the X4 Pro release firmware on this machine and publish the web installer
# (site/ plus the full image and manifest) to the gh-pages branch, then make the GitHub release
# for this version (tag plus bins; the device's update check reads it). GitHub only serves the files.
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
SHA=$(sha256sum vaultink-x4pro-factory.bin | cut -d' ' -f1)
echo "$SHA  vaultink-x4pro-factory.bin"

# The release: the full image for USB flashing, and the app image the update check downloads
# (vaultink-<version>-x4pro.bin, see src/network/OtaUpdater.cpp).
cd "$ROOT"
cp .pio/build/x4pro-gh_release/firmware.factory.bin "$OUT/vaultink-${VERSION}-x4pro-factory.bin"
cp .pio/build/x4pro-gh_release/firmware.bin "$OUT/vaultink-${VERSION}-x4pro.bin"
if gh release view "$VERSION" >/dev/null 2>&1; then
  gh release upload "$VERSION" --clobber "$OUT/vaultink-${VERSION}-x4pro-factory.bin" "$OUT/vaultink-${VERSION}-x4pro.bin"
else
  git tag -a "$VERSION" -m "VaultInk ${VERSION}" HEAD 2>/dev/null || true
  git push -q origin "$VERSION"
  gh release create "$VERSION" --title "VaultInk ${VERSION}" \
    --notes "Not tested on real hardware yet. Install from https://somnius.github.io/VaultInk/ or flash the factory bin at 0x0. sha256 of the factory bin: ${SHA}

Made by Lefteris Iliadis (https://lefteros.com). More about VaultInk, and donations: https://lefteros.com/en/apps/vaultink" \
    "$OUT/vaultink-${VERSION}-x4pro-factory.bin" "$OUT/vaultink-${VERSION}-x4pro.bin"
  echo "Release ${VERSION} created; edit its notes on GitHub if you want more than the default text."
fi
