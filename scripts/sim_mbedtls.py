# Simulator only: the device gets mbedtls from ESP-IDF, the simulator links OpenSSL. LiveSync's
# crypto (lib/LiveSync) is written against mbedtls, so build mbedtls's library here as well: the
# ESP-IDF copy when it's installed (same sources as the device), else a pinned clone of the same
# release under .pio/.
import glob
import os
import subprocess

Import("env")

VERSION = "v3.6.6"
candidates = glob.glob(os.path.expanduser("~/.platformio*/packages/framework-espidf/components/mbedtls/mbedtls"))
root = candidates[0] if candidates else os.path.join(env.subst("$PROJECT_DIR"), ".pio", "mbedtls-" + VERSION)
if not candidates and not os.path.isdir(os.path.join(root, "library")):
    subprocess.check_call(["git", "clone", "--quiet", "--depth", "1", "--branch", VERSION,
                           "https://github.com/Mbed-TLS/mbedtls.git", root])

include = os.path.join(root, "include")
for e in (env, DefaultEnvironment()):
    e.Prepend(CPPPATH=[include])  # before the simulator's own small mbedtls shims
env.BuildSources(os.path.join("$BUILD_DIR", "mbedtls"), os.path.join(root, "library"), src_filter=["+<*.c>"])
