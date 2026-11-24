#!/usr/bin/env bash
# Republish the web installer page (site/, with debug.html and debuglib.js) to gh-pages without a
# new release: the firmware image and manifest stay those of the current version's release
# (downloaded from it, not rebuilt). The developer build is republished too when the release has
# its image (vaultink-<version>-x4pro-dev-factory.bin); without it there is no manifest-dev.json
# and the page says the developer build is not published for this version.
#
#   scripts/deploy_site_only.sh
set -euo pipefail

# The backup and restore page is held back until it has worked on a real reader (it failed to connect on the
# maintainer's own computer, 2026-11-14). Its files stay in site/ but are not published, and the link on the
# install page is cut out, unless BACKUP_PAGE=1.
publish_backup_page() { # $1 = folder that is about to be published
  if [[ "${BACKUP_PAGE:-0}" == 1 ]]; then
    for page in backup.html backuplib.js; do
      [[ -s "$1/$page" ]] || { echo "site/$page is missing: the backup and restore page would not be published." >&2; exit 1; }
    done
  else
    rm -f "$1/backup.html" "$1/backuplib.js"
    python3 - "$1/index.html" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p).read()
s = re.sub(r"[ \t]*<!-- backup-page:start -->.*?<!-- backup-page:end -->\n", "", s, flags=re.S)
open(p, "w").write(s)
PY
    if grep -q "backup.html" "$1/index.html"; then
      echo "The install page still links to backup.html." >&2
      exit 1
    fi
  fi
}

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
# The X3/X4 (ESP32-C3) image, from 0.79.0 on. esp-web-tools picks the build by the chip it finds.
C3_BUILD=""
if gh release view "$VERSION" --json assets --jq '.assets[].name' | grep -q -x -F "vaultink-${VERSION}-x3-x4-factory.bin"; then
  gh release download "$VERSION" -p "vaultink-${VERSION}-x3-x4-factory.bin" -D "$OUT"
  [[ -s "$OUT/vaultink-${VERSION}-x3-x4-factory.bin" ]] || { echo "X3/X4 image download failed; nothing was published." >&2; exit 1; }
  mv "$OUT/vaultink-${VERSION}-x3-x4-factory.bin" "$OUT/vaultink-x3-x4-factory.bin"
  C3_BUILD=',
    { "chipFamily": "ESP32-C3", "parts": [ { "path": "vaultink-x3-x4-factory.bin", "offset": 0 } ] }'
fi
cat > "$OUT/manifest.json" <<EOT
{
  "name": "VaultInk",
  "version": "${VERSION}",
  "built": "$(git log -1 --format=%cs "$VERSION")",
  "commit": "$(git rev-parse --short "$VERSION^{commit}")",
  "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32-S3", "parts": [ { "path": "vaultink-x4pro-factory.bin", "offset": 0 } ] }${C3_BUILD}
  ]
}
EOT
# Ask the release what it has, so that "no developer image in this release" (a release made before
# the developer build: a warning) and "the download failed" (stop, publish nothing) are told apart.
DEV_ASSET="vaultink-${VERSION}-x4pro-dev-factory.bin"
if ! ASSETS=$(gh release view "$VERSION" --json assets --jq '.assets[].name'); then
  echo "Could not list the files of release $VERSION; nothing was published." >&2
  exit 1
fi
if grep -q -x -F "$DEV_ASSET" <<<"$ASSETS"; then
  if ! gh release download "$VERSION" -p "$DEV_ASSET" -D "$OUT" || [[ ! -s "$OUT/$DEV_ASSET" ]]; then
    echo "Release $VERSION has $DEV_ASSET but it could not be downloaded; nothing was published." >&2
    exit 1
  fi
  mv "$OUT/vaultink-${VERSION}-x4pro-dev-factory.bin" "$OUT/vaultink-x4pro-dev-factory.bin"
  cat > "$OUT/manifest-dev.json" <<EOT
{
  "name": "VaultInk (developer build)",
  "version": "${VERSION}-dev",
  "built": "$(git log -1 --format=%cs "$VERSION")",
  "commit": "$(git rev-parse --short "$VERSION^{commit}")",
  "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32-S3", "parts": [ { "path": "vaultink-x4pro-dev-factory.bin", "offset": 0 } ] }
  ]
}
EOT
else
  echo "Release $VERSION has no developer build image: the page will not offer it." >&2
fi
for page in debug.html debuglib.js; do
  [[ -s "$OUT/$page" ]] || { echo "site/$page is missing: the debug page would not be published." >&2; exit 1; }
done
publish_backup_page "$OUT"
touch "$OUT/.nojekyll"
cd "$OUT"
git init -q -b gh-pages
git add -A
git commit -q -m "Web installer ${VERSION}, page updated ($(git -C "$ROOT" rev-parse --short HEAD))"
git push -q -f "$REMOTE" gh-pages
echo "Published the page for ${VERSION}: https://somnius.github.io/VaultInk/"
