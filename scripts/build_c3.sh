#!/usr/bin/env bash
# Builds the ESP32-C3 release (env gh_release: Xteink X3 and X4, one image).
#
# Why the detour: the platform (pioarduino 55.03.311) wants tool-scons 4.8.1 in platform.json,
# PlatformIO Core 6.2 runs SCons 4.11.1, and an env with custom_sdkconfig (the C3 ones) makes the
# platform reinstall tool-scons in the middle of the build: the link then fails with
# "No module named 'SCons.Tool.FortranCommon'". For this one build, platform.json is told 4.11.1
# so the version check passes; the file is put back afterwards, whatever the build does.
#
#   scripts/build_c3.sh [pio run options...]
set -euo pipefail
cd "$(dirname "$0")/.."
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
PJ="$HOME/.platformio/platforms/espressif32/platform.json"
BACKUP=$(mktemp)
cp "$PJ" "$BACKUP"
trap 'cp "$BACKUP" "$PJ"; rm -f "$BACKUP"' EXIT
python3 - "$PJ" <<'PY'
import sys
p = sys.argv[1]
t = open(p).read()
old = ('"package-version": "4.40801.0",\n      "version": '
       '"https://github.com/pioarduino/registry/releases/download/0.0.1/scons-4.8.1.zip"')
new = ('"package-version": "4.41101.0",\n      "version": '
       '"https://github.com/pioarduino/scons/releases/download/4.11.1/scons-local-4.11.1.tar.gz"')
if old in t:
    open(p, "w").write(t.replace(old, new))
PY
taskset -c "0-$(( $(nproc) * 3 / 4 - 1 ))" nice -n 10 "$PIO" run -e gh_release "$@"
