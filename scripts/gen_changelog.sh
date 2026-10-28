#!/bin/bash
# Rebuilds CHANGELOG.md from this repository's GitHub releases (newest first), without the
# install and credit lines every release ends with. Run before a release, after its notes exist.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=CHANGELOG.md
{
  echo "# VaultInk changelog"
  echo
  echo "Every release, newest first. The device shows the new entries once after an update."
  # The version being built has no GitHub release yet: its notes come from the local draft.
  version=$(grep -m1 '^version' platformio.ini | sed 's/.*= *//')
  draft=.work/release-notes/$version.md
  if ! gh release view "$version" > /dev/null 2>&1 && [[ -s "$draft" ]]; then
    echo
    echo "## $version ($(date +%F))"
    echo
    grep -v -E '^(Install from|Made by Lefteris)' "$draft" | cat -s
  fi
  for tag in $(gh release list --limit 200 --json tagName --jq '.[].tagName' | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | sort -t. -k1,1nr -k2,2nr -k3,3nr); do
    date=$(gh release view "$tag" --json publishedAt --jq '.publishedAt[:10]')
    echo
    echo "## $tag ($date)"
    echo
    gh release view "$tag" --json body --jq .body | grep -v -E '^(Install from|Made by Lefteris)' | sed -e 's/\r$//' | cat -s
  done
} > "$OUT"
echo "Wrote $OUT ($(grep -c '^## ' "$OUT") releases)"
