#!/bin/bash
# Builds the firmware's SSH client for this computer and runs it against a throwaway OpenSSH
# server on 127.0.0.1 (started here as the current user, stopped at the end). Nothing leaves the
# machine. What it proves and what it cannot is in docs/ssh.md, "Checks on the computer".
#
#   test/ssh_live/run.sh            # needs sshd, ssh-keygen, gcc/g++ and .pio/libdeps (pio pkg install)
#   WOLFSSL_SRC=/path/to/Arduino-wolfSSL/src BUILD_DIR=/tmp/x test/ssh_live/run.sh
#
# The wolfSSL sources and -D flags are the device's: Arduino-wolfSSL 5.7.2 with its
# user_settings.h as patched by scripts/patch_wolfssl.py, the base flags of platformio.ini and
# [vaultink_ssh]. Only the CPU differs (and SP_WORD_SIZE with it).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WOLFSSL_SRC=${WOLFSSL_SRC:-$REPO/.pio/libdeps/x4pro/Arduino-wolfSSL/src}
BUILD_DIR=${BUILD_DIR:-$REPO/.work/ssh_live}
PORT=${PORT:-22622}
JOBS=${JOBS:-8}
SSHD=$(command -v sshd || echo /usr/sbin/sshd)

[ -f "$WOLFSSL_SRC/user_settings.h" ] || { echo "no wolfSSL sources at $WOLFSSL_SRC"; exit 2; }
[ -x "$SSHD" ] || { echo "no sshd"; exit 2; }
grep -q 'CrossPoint wolfSSL compatibility overrides' "$WOLFSSL_SRC/user_settings.h" ||
  echo "note: user_settings.h is not patched by scripts/patch_wolfssl.py (FP_MAX_BITS, DH)"

mkdir -p "$BUILD_DIR/obj" || exit 2
FLAGS="-Os -w -DWOLFSSL_KEY_GEN -DWC_RC2 -DNO_WOLFSSL_ESP32_CRYPT_RSA_PRI -DWOLFSSL_USER_SETTINGS -DWOLFSSL_OPTIONS_H
 -DWOLFSSL_CLIENT_EXAMPLE -DWOLFSSL_TLS13 -DWOLFSSL_HAVE_SP_ECC -DWOLFSSL_SP_SMALL -DHAVE_FFDHE_2048 -DHAVE_CURVE25519
 -DHAVE_SNI -DVAULTINK_SSH=1 -DWOLFSSL_WOLFSSH -DHAVE_ED25519 -DWOLFSSL_ED25519_STREAMING_VERIFY -DWOLFSSL_SHA512
 -I$WOLFSSL_SRC -I$REPO/lib/wolfssh -I$REPO/lib/Ssh -I$REPO/lib/SshClient -I$REPO/lib/RemoteFiles -I$REPO/lib/TextInput"
FLAGS=$(echo $FLAGS)

echo "== build (wolfCrypt 5.7.2 sources, wolfSSH, lib/SshClient)"
ls "$WOLFSSL_SRC"/wolfcrypt/src/*.c | grep -v -E '/(evp|bio|misc|asm|fips|fips_test|selftest|wolfcrypt_first|wolfcrypt_last)\.c$|/sp_(arm|cortex|x86|dsp|sm2|c64)' > "$BUILD_DIR/c_files"
ls "$REPO"/lib/wolfssh/vaultink/ws_*.c >> "$BUILD_DIR/c_files"
export FLAGS BUILD_DIR
nice -n 10 xargs -P "$JOBS" -I{} sh -c 'gcc $FLAGS -c "{}" -o "$BUILD_DIR/obj/$(basename "{}" .c).o" || echo "COMPILE FAILED {}"' < "$BUILD_DIR/c_files" | tee "$BUILD_DIR/c_errors"
[ -s "$BUILD_DIR/c_errors" ] && exit 1
gcc -c "$HERE/host_stubs.c" -o "$BUILD_DIR/obj/host_stubs.o" || exit 1
nice -n 10 g++ -std=gnu++20 -fno-exceptions $FLAGS -Wall -Wextra \
  "$HERE/ssh_live_test.cpp" "$REPO/lib/SshClient/SshSession.cpp" "$REPO/lib/Ssh/SshKey.cpp" \
  "$REPO/lib/Ssh/SshKnownHosts.cpp" "$REPO/lib/Ssh/SshTerminal.cpp" "$BUILD_DIR"/obj/*.o -lm \
  -o "$BUILD_DIR/ssh_live_test" || exit 1

echo "== throwaway sshd on 127.0.0.1:$PORT"
D=$BUILD_DIR/sshd
rm -rf "$D"; mkdir -p "$D" && chmod 700 "$D"
ssh-keygen -q -t ed25519 -N '' -f "$D/host_ed25519" || exit 2
ssh-keygen -l -f "$D/host_ed25519.pub" > "$D/fingerprint"
: > "$D/authorized_keys"; chmod 600 "$D/authorized_keys"
cat > "$D/sshd_config" <<EOF
Port $PORT
ListenAddress 127.0.0.1
HostKey $D/host_ed25519
PidFile $D/sshd.pid
AuthorizedKeysFile $D/authorized_keys
StrictModes no
UsePAM no
PasswordAuthentication yes
KbdInteractiveAuthentication no
PubkeyAuthentication yes
Subsystem sftp internal-sftp
LogLevel DEBUG1
EOF
"$SSHD" -D -e -f "$D/sshd_config" 2> "$D/sshd.log" &
SSHD_PID=$!
trap 'kill $SSHD_PID 2>/dev/null' EXIT
for i in 1 2 3 4 5 6 7 8 9 10; do
  (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null && break
  sleep 0.3
done

echo "== run"
"$BUILD_DIR/ssh_live_test" "$PORT" "$(id -un)" "$D" "$D/fingerprint"
RESULT=$?
echo "== what the server negotiated (sshd log)"
grep -E "kex: (algorithm|host key|client->server)|Accepted publickey|Failed (password|publickey)" "$D/sshd.log" | sed 's/^/   /' | sort | uniq -c | sort -rn | head -12
exit $RESULT
