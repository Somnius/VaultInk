#!/usr/bin/env bash
# Build the X4 Pro release firmware on this machine and publish the web installer
# (site/ plus the full image and manifest) to the gh-pages branch, then make the GitHub release
# for this version (tag plus bins; the device's update check reads it). GitHub only serves the files.
#
# It also builds the developer build users may install for debugging (env x4pro-dev-user, the
# read-only link of docs/devlink.md) and publishes it as manifest-dev.json for the "Developer
# build" button of the page and for debug.html.
#
#   scripts/deploy_web_installer.sh            # build, then publish
#   scripts/deploy_web_installer.sh --dry-run  # build, put the files together, publish nothing
#   PIO=/path/to/pio scripts/deploy_web_installer.sh
set -euo pipefail

# --dry-run (or DRY_RUN=1): everything up to the first push. The files that would go to gh-pages
# and to the release are left in a folder that is printed at the end; nothing is pushed, tagged or
# uploaded, and release notes are not required.
DRY_RUN="${DRY_RUN:-0}"
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=1 ;;
    *) echo "Unknown argument: $arg (only --dry-run)" >&2; exit 2 ;;
  esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIO="${PIO:-pio}"
REMOTE="${REMOTE:-https://github.com/Somnius/VaultInk.git}"
cd "$ROOT"

# Release notes are written per version (what changed, what was tested on a device); no default.
VERSION_PRE=$(grep -m1 '^version' platformio.ini | sed 's/.*= *//')
NOTES="$ROOT/.work/release-notes/${VERSION_PRE}.md"
if [[ "$DRY_RUN" == 1 ]]; then
  [[ -s "$NOTES" ]] || echo "Dry run: no release notes yet ($NOTES); a real run needs them."
else
  if [[ ! -s "$NOTES" ]]; then
    echo "Write the release notes first: $NOTES" >&2
    exit 1
  fi

  # The device's update screen drops notes longer than 2048 bytes once JSON-escaped (0.54.0 showed none).
  NOTES_JSON=$(python3 -c 'import json,sys;print(len(json.dumps(open(sys.argv[1]).read())))' "$NOTES")
  if (( NOTES_JSON > 1950 )); then
    echo "Release notes are $NOTES_JSON bytes escaped; the device shows at most about 1950. Shorten $NOTES." >&2
    exit 1
  fi
fi

if [[ -n "$(git status --porcelain -- src lib platformio.ini partitions_vaultink.csv)" ]]; then
  echo "Uncommitted firmware changes; commit them first so the installer matches a commit." >&2
  exit 1
fi

# One build after the other, never two at once.
taskset -c "0-$(( $(nproc) * 3 / 4 - 1 ))" nice -n 10 "$PIO" run -e x4pro-gh_release
taskset -c "0-$(( $(nproc) * 3 / 4 - 1 ))" nice -n 10 "$PIO" run -e x4pro-dev-user

# The developer build that goes to users must be the read-only link. The commands of the
# developer's own builds (keys, touches, SD files, flashing) have this HELP text; it must not be
# in the image. (The text is stored as it is in an app image: not compressed, not encrypted.)
# The second text is the usage line of CMD:FETCH (src/util/DevLinkDevice.cpp), the command that
# downloads a file: it is in the full link only, after PUT in the same HELP text.
DEV_APP=.pio/build/x4pro-dev-user/firmware.bin
if grep -a -q -F -e 'KEY TAP SWIPE FLASH LS GET PUT' -e 'usage: FETCH http://' "$DEV_APP"; then
  echo "x4pro-dev-user holds the full developer link (VAULTINK_DEVLINK_FULL). It must not be published." >&2
  exit 1
fi
if ! grep -a -q -F 'PING HELP STATUS LOG BENCH REBOOT SCREENSHOT' "$DEV_APP"; then
  echo "Warning: the read-only link's HELP text was not found in $DEV_APP; check the build before publishing." >&2
fi

VERSION=$(grep -m1 '^version' platformio.ini | sed 's/.*= *//')
OUT=$(mktemp -d)
# A dry run leaves the folder for a look; a real run removes it.
[[ "$DRY_RUN" == 1 ]] || trap 'rm -rf "$OUT"' EXIT
PAGES="$OUT/gh-pages"   # what is pushed to the gh-pages branch, and nothing else
RELEASE="$OUT/release"  # what is attached to the GitHub release
mkdir "$PAGES" "$RELEASE"
cp site/* "$PAGES/"
for page in debug.html debuglib.js; do
  [[ -s "$PAGES/$page" ]] || { echo "site/$page is missing: the debug page would not be published." >&2; exit 1; }
done
cp .pio/build/x4pro-gh_release/firmware.factory.bin "$PAGES/vaultink-x4pro-factory.bin"
# The developer build's full image, made the same way (firmware.factory.bin of its own env).
cp .pio/build/x4pro-dev-user/firmware.factory.bin "$PAGES/vaultink-x4pro-dev-factory.bin"
cat > "$PAGES/manifest-dev.json" <<EOF
{
  "name": "VaultInk (developer build)",
  "version": "${VERSION}-dev",
  "built": "$(date +%Y-%m-%d)",
  "commit": "$(git rev-parse --short HEAD)",
  "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32-S3", "parts": [ { "path": "vaultink-x4pro-dev-factory.bin", "offset": 0 } ] }
  ]
}
EOF
cat > "$PAGES/manifest.json" <<EOF
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
touch "$PAGES/.nojekyll"

# The release's files. The developer build goes up as its full image only, under a name the
# update check cannot take for the update file: it asks for exactly vaultink-<version>-x4pro.bin
# (src/network/OtaUpdater.cpp, compared whole in lib/JsonParser/ReleaseJsonParser.cpp).
# scripts/deploy_site_only.sh downloads it from the release to republish the page.
RELEASE_FILES=(
  "$RELEASE/vaultink-${VERSION}-x4pro-factory.bin"
  "$RELEASE/vaultink-${VERSION}-x4pro.bin"
  "$RELEASE/vaultink-${VERSION}-x4pro-dev-factory.bin"
)
cp .pio/build/x4pro-gh_release/firmware.factory.bin "${RELEASE_FILES[0]}"
cp .pio/build/x4pro-gh_release/firmware.bin "${RELEASE_FILES[1]}"
cp .pio/build/x4pro-dev-user/firmware.factory.bin "${RELEASE_FILES[2]}"

if [[ "$DRY_RUN" == 1 ]]; then
  echo "Dry run for ${VERSION}: nothing was pushed, tagged or uploaded."
  echo "Would go to gh-pages (from $PAGES):"
  (cd "$PAGES" && ls -A | grep -v -E '\.(png|gif|svg)$' | sed 's/^/  /')
  echo "  ... and $(cd "$PAGES" && ls | grep -c -E '\.(png|gif|svg)$') pictures"
  echo "manifest.json:"
  sed 's/^/  /' "$PAGES/manifest.json"
  echo "manifest-dev.json:"
  sed 's/^/  /' "$PAGES/manifest-dev.json"
  echo "Would go to the release ${VERSION}:"
  for file in "${RELEASE_FILES[@]}"; do echo "  $(basename "$file")"; done
  echo "The files are in $OUT (delete it when you have looked)."
  exit 0
fi

cd "$PAGES"
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
if gh release view "$VERSION" >/dev/null 2>&1; then
  gh release upload "$VERSION" --clobber "${RELEASE_FILES[@]}"
else
  # A tag of this name may already exist (CrossPoint's history had 0.x tags): never reuse one
  # that points anywhere but HEAD.
  if git rev-parse -q --verify "refs/tags/$VERSION" >/dev/null; then
    if [[ "$(git rev-list -n1 "$VERSION")" != "$(git rev-parse HEAD)" ]]; then
      echo "Tag $VERSION exists and is not HEAD; refusing to release on it." >&2
      exit 1
    fi
  else
    git tag -a "$VERSION" -m "VaultInk ${VERSION}" HEAD
  fi
  git push -q origin "refs/tags/$VERSION"
  gh release create "$VERSION" --title "VaultInk ${VERSION}" --notes-file "$NOTES" --latest \
    "${RELEASE_FILES[@]}"
  echo "Release ${VERSION} created."
fi

# The release must carry this machine's build. Nothing on GitHub may rebuild or replace it.
CHECK=$(mktemp -d)
gh release download "$VERSION" -p "vaultink-${VERSION}-x4pro.bin" -D "$CHECK" --clobber
if [[ "$(sha256sum < "$CHECK/vaultink-${VERSION}-x4pro.bin")" != "$(sha256sum < .pio/build/x4pro-gh_release/firmware.bin)" ]]; then
  echo "Release ${VERSION}: the update file on GitHub is NOT the local build." >&2
  exit 1
fi
rm -rf "$CHECK"
echo "Release ${VERSION}: update file matches the local build."
