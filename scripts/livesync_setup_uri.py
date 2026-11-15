#!/usr/bin/env python3
"""Re-encrypts an Obsidian Self-hosted LiveSync Setup URI with a new passphrase.

A Setup URI holds the vault's settings (server, user, password, the end-to-end passphrase),
encrypted with the passphrase chosen when it was copied in Obsidian. This script opens one with
that passphrase and writes the same settings under a new passphrase, for example a short one that
is easy to type on the reader (Settings > Vaults > LiveSync > Finish setup).

    python3 scripts/livesync_setup_uri.py --uri-file old-uri.txt --out new-uri.txt
    python3 scripts/livesync_setup_uri.py --self-test

Secrets never come in on the command line and are never printed:
  the Setup URI        --uri-file FILE, or $LIVESYNC_SETUP_URI, or a prompt
  its passphrase       --passphrase-file FILE, or $LIVESYNC_SETUP_PASSPHRASE, or a hidden prompt
  the new passphrase   --new-passphrase-file FILE, or $LIVESYNC_NEW_PASSPHRASE, or a hidden prompt (twice)
The new URI goes to --out FILE (created readable by you only) or, without --out, to standard output.
The settings are only held in memory; they are not parsed, shown or written anywhere in the clear.

The format is the one lib/LiveSync/LiveSyncCrypto.cpp reads (pinned from the plugin's library):
  "obsidian://setuplivesync?settings=" + encodeURIComponent("%$" + base64(
      pbkdf2Salt[32] | iv[12] | hkdfSalt[32] | AES-256-GCM ciphertext | tag[16]))
  master = PBKDF2-HMAC-SHA256(passphrase, pbkdf2Salt, 310000 rounds, 32 bytes)
  key    = HKDF-SHA256(master, hkdfSalt, info "")            no additional data
A URI copied with the time-bound passphrase form is opened too (this week and the two before);
the new one is always the plain form, which is what the reader's passphrase prompt expects.

Only the Python standard library is used, so AES-GCM is written out below. It is not constant
time: run it on your own computer, not on a shared one.
"""

import argparse
import base64
import getpass
import hashlib
import hmac
import json
import os
import sys
import time
import urllib.parse

PREFIX = "obsidian://setuplivesync?settings="
PBKDF2_ITERATIONS = 310000
PBKDF2_SALT_LEN = 32
IV_LEN = 12
HKDF_SALT_LEN = 32
TAG_LEN = 16
WEEK_MS = 604800000
# encodeURIComponent leaves these unescaped (besides letters and digits).
URI_SAFE = "-_.!~*'()"


class SetupUriError(Exception):
    pass


# --- AES-256 (encrypt direction only: GCM needs nothing else) -----------------------------------


def _build_sbox():
    # Multiplicative inverse in GF(2^8), then the affine transform (FIPS-197 5.1.1).
    exp, log = [0] * 510, [0] * 256
    x = 1
    for i in range(255):
        exp[i] = x
        log[x] = i
        x ^= ((x << 1) ^ (0x11B if x & 0x80 else 0)) & 0x1FF  # multiply by 3
        x &= 0xFF
    for i in range(255, 510):
        exp[i] = exp[i - 255]
    sbox = [0] * 256
    for value in range(256):
        inv = 0 if value == 0 else exp[255 - log[value]]
        out = inv
        for shift in (1, 2, 3, 4):
            out ^= ((inv << shift) | (inv >> (8 - shift))) & 0xFF
        sbox[value] = out ^ 0x63
    return sbox


_SBOX = _build_sbox()
_XTIME = [((v << 1) ^ (0x1B if v & 0x80 else 0)) & 0xFF for v in range(256)]


def _expand_key(key):
    if len(key) != 32:
        raise ValueError("AES-256 needs a 32-byte key")
    words = [list(key[i : i + 4]) for i in range(0, 32, 4)]
    rcon = 1
    for i in range(8, 60):
        temp = list(words[i - 1])
        if i % 8 == 0:
            temp = [_SBOX[b] for b in temp[1:] + temp[:1]]
            temp[0] ^= rcon
            rcon = _XTIME[rcon]
        elif i % 8 == 4:
            temp = [_SBOX[b] for b in temp]
        words.append([a ^ b for a, b in zip(words[i - 8], temp)])
    return [sum(words[r * 4 : r * 4 + 4], []) for r in range(15)]


def _encrypt_block(round_keys, block):
    state = [b ^ k for b, k in zip(block, round_keys[0])]
    for rnd in range(1, 15):
        state = [_SBOX[b] for b in state]
        # ShiftRows: the state is column-major, row r of column c is state[4 * c + r].
        state = [state[(4 * (c + r) + r) % 16] for c in range(4) for r in range(4)]
        if rnd != 14:
            mixed = []
            for c in range(4):
                a = state[4 * c : 4 * c + 4]
                total = a[0] ^ a[1] ^ a[2] ^ a[3]
                mixed += [a[r] ^ total ^ _XTIME[a[r] ^ a[(r + 1) % 4]] for r in range(4)]
            state = mixed
        state = [b ^ k for b, k in zip(state, round_keys[rnd])]
    return bytes(state)


# --- GCM (NIST SP 800-38D), 96-bit IV, no additional data ---------------------------------------


def _gf_mult(x, y):
    z = 0
    v = y
    for bit in range(127, -1, -1):
        if (x >> bit) & 1:
            z ^= v
        v = (v >> 1) ^ (0xE1 << 120) if v & 1 else v >> 1
    return z


def _ghash(h, data):
    y = 0
    for i in range(0, len(data), 16):
        block = data[i : i + 16].ljust(16, b"\0")
        y = _gf_mult(y ^ int.from_bytes(block, "big"), h)
    return y


def _gcm(key, iv, data):
    """Returns (data XOR keystream, function(ciphertext) -> tag)."""
    if len(iv) != IV_LEN:
        raise ValueError("this GCM takes a 12-byte IV")
    round_keys = _expand_key(key)
    h = int.from_bytes(_encrypt_block(round_keys, bytes(16)), "big")
    j0 = iv + b"\x00\x00\x00\x01"
    out = bytearray()
    for i in range(0, len(data), 16):
        counter = iv + (i // 16 + 2).to_bytes(4, "big")
        stream = _encrypt_block(round_keys, counter)
        out += bytes(a ^ b for a, b in zip(data[i : i + 16], stream))

    def tag_for(ciphertext):
        padded = ciphertext + bytes(-len(ciphertext) % 16)
        lengths = (0).to_bytes(8, "big") + (len(ciphertext) * 8).to_bytes(8, "big")
        s = _ghash(h, padded + lengths)
        mask = int.from_bytes(_encrypt_block(round_keys, j0), "big")
        return (s ^ mask).to_bytes(16, "big")

    return bytes(out), tag_for


def aes_gcm_encrypt(key, iv, plain):
    cipher, tag_for = _gcm(key, iv, plain)
    return cipher + tag_for(cipher)


def aes_gcm_decrypt(key, iv, cipher_and_tag):
    """The plaintext, or None when the tag doesn't match (wrong key or damaged data)."""
    if len(cipher_and_tag) < TAG_LEN:
        return None
    cipher, tag = cipher_and_tag[:-TAG_LEN], cipher_and_tag[-TAG_LEN:]
    plain, tag_for = _gcm(key, iv, cipher)
    return plain if hmac.compare_digest(tag_for(cipher), tag) else None


# --- The Setup URI ------------------------------------------------------------------------------


def hkdf_sha256(master, salt, length=32):
    """HKDF (RFC 5869) with an empty info, as the plugin calls it."""
    prk = hmac.new(salt, master, hashlib.sha256).digest()
    out, block, counter = b"", b"", 1
    while len(out) < length:
        block = hmac.new(prk, block + bytes([counter]), hashlib.sha256).digest()
        out += block
        counter += 1
    return out[:length]


def derive_master(passphrase, pbkdf2_salt):
    return hashlib.pbkdf2_hmac("sha256", passphrase.encode("utf-8"), pbkdf2_salt, PBKDF2_ITERATIONS, 32)


def ephemeral_passphrase(passphrase, week):
    """The time-bound form: hex(HMAC-SHA256(SHA256(pass), '["livesync/setup-uri","tb1","ephemeral",<week>]'))."""
    message = '["livesync/setup-uri","tb1","ephemeral",%d]' % week
    return hmac.new(hashlib.sha256(passphrase.encode("utf-8")).digest(), message.encode(), hashlib.sha256).hexdigest()


def blob_of(uri):
    """The "%$..." string inside a Setup URI (or the string itself when it is already the blob)."""
    text = uri.strip()
    if text.startswith(PREFIX):
        text = urllib.parse.unquote(text[len(PREFIX) :])
    if not text.startswith("%$"):
        raise SetupUriError("This is not a LiveSync Setup URI (obsidian://setuplivesync?settings=...).")
    return text


def decrypt_blob(blob, passphrase):
    """The settings bytes, or None when the passphrase doesn't open it."""
    try:
        box = base64.b64decode(blob[2:], validate=True)
    except ValueError as error:
        raise SetupUriError("The Setup URI is damaged (not base64).") from error
    if len(box) < PBKDF2_SALT_LEN + IV_LEN + HKDF_SALT_LEN + TAG_LEN:
        raise SetupUriError("The Setup URI is too short.")
    pbkdf2_salt = box[:PBKDF2_SALT_LEN]
    iv = box[PBKDF2_SALT_LEN : PBKDF2_SALT_LEN + IV_LEN]
    hkdf_salt = box[PBKDF2_SALT_LEN + IV_LEN : PBKDF2_SALT_LEN + IV_LEN + HKDF_SALT_LEN]
    rest = box[PBKDF2_SALT_LEN + IV_LEN + HKDF_SALT_LEN :]
    key = hkdf_sha256(derive_master(passphrase, pbkdf2_salt), hkdf_salt)
    return aes_gcm_decrypt(key, iv, rest)


def decode_setup_uri(uri, passphrase, now=None):
    """The settings bytes of a Setup URI. Tries the passphrase as typed, then the time-bound form."""
    blob = blob_of(uri)
    plain = decrypt_blob(blob, passphrase)
    if plain is None:
        week = int((time.time() if now is None else now) * 1000) // WEEK_MS
        for candidate in (week, week - 1, week - 2):
            plain = decrypt_blob(blob, ephemeral_passphrase(passphrase, candidate))
            if plain is not None:
                break
    if plain is None:
        raise SetupUriError("Wrong passphrase for this Setup URI (or the URI is damaged).")
    return plain


def encode_setup_uri(settings, passphrase, random_bytes=os.urandom):
    """A Setup URI holding `settings` (bytes) under `passphrase`."""
    pbkdf2_salt = random_bytes(PBKDF2_SALT_LEN)
    iv = random_bytes(IV_LEN)
    hkdf_salt = random_bytes(HKDF_SALT_LEN)
    key = hkdf_sha256(derive_master(passphrase, pbkdf2_salt), hkdf_salt)
    box = pbkdf2_salt + iv + hkdf_salt + aes_gcm_encrypt(key, iv, settings)
    blob = "%$" + base64.b64encode(box).decode("ascii")
    return PREFIX + urllib.parse.quote(blob, safe=URI_SAFE)


# --- Checks -------------------------------------------------------------------------------------

DUMMY_SETTINGS = {
    "couchDB_URI": "https://couch.example.org",
    "couchDB_USER": "dummy-user",
    "couchDB_PASSWORD": "dummy-password",
    "couchDB_DBNAME": "dummy-vault",
    "encrypt": True,
    "passphrase": "dummy-e2ee-passphrase",
    "usePathObfuscation": True,
}
FIXTURE_PASSPHRASE = "dummy-new-passphrase"


def _counting_bytes(count):
    """Fixed "randomness" for the fixture, so the file is the same every time it is made."""
    return bytes((7 * i + 3) & 0xFF for i in range(count))


def self_test():
    # AES-256 (FIPS-197 C.3) and AES-256-GCM (the GCM specification's test case 15, which has no
    # additional data).
    key = bytes(range(32))
    block = _encrypt_block(_expand_key(key), bytes.fromhex("00112233445566778899aabbccddeeff"))
    assert block.hex() == "8ea2b7ca516745bfeafc49904b496089", "AES-256 block"
    key = bytes.fromhex("feffe9928665731c6d6a8f9467308308" * 2)
    iv = bytes.fromhex("cafebabefacedbaddecaf888")
    plain = bytes.fromhex(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255"
    )
    sealed = aes_gcm_encrypt(key, iv, plain)
    assert sealed[-16:].hex() == "b094dac5d93471bdec1a502270e3cc6c", "AES-256-GCM tag"
    assert sealed[:16].hex() == "522dc1f099567d07f47f37a32a84427d", "AES-256-GCM ciphertext"
    assert aes_gcm_decrypt(key, iv, sealed) == plain
    # HKDF-SHA256, RFC 5869 test case 3 (empty salt and info).
    okm = hkdf_sha256(bytes.fromhex("0b" * 22), b"", 42)
    assert okm.hex().startswith("8da4e775a563c18f715f802a063c5a31"), "HKDF"
    # A dummy settings JSON through a Setup URI and back, and the wrong passphrase refused.
    settings = json.dumps(DUMMY_SETTINGS, separators=(",", ":")).encode()
    uri = encode_setup_uri(settings, "first dummy passphrase")
    assert uri.startswith(PREFIX + "%25%24")
    assert decode_setup_uri(uri, "first dummy passphrase") == settings
    moved = encode_setup_uri(decode_setup_uri(uri, "first dummy passphrase"), "second dummy passphrase")
    assert decode_setup_uri(moved, "second dummy passphrase") == settings
    try:
        decode_setup_uri(moved, "first dummy passphrase")
    except SetupUriError:
        pass
    else:
        raise AssertionError("the old passphrase still opens the new URI")
    # The time-bound form of a passphrase opens too.
    now = 1_760_000_000
    timed = encode_setup_uri(settings, ephemeral_passphrase("dummy", now * 1000 // WEEK_MS))
    assert decode_setup_uri(timed, "dummy", now=now) == settings
    return True


def fixture_header():
    """A C++ header for test/livesync: a URI made here that the firmware's code must open."""
    settings = json.dumps(DUMMY_SETTINGS, separators=(",", ":"))
    uri = encode_setup_uri(settings.encode(), FIXTURE_PASSPHRASE, random_bytes=_counting_bytes)

    def literal(text):
        pieces = [text[i : i + 96] for i in range(0, len(text), 96)]
        return "\n".join("    " + json.dumps(piece) for piece in pieces)

    return (
        "#pragma once\n\n"
        "// Made by `python3 scripts/livesync_setup_uri.py --write-fixture test/livesync/python_uri_fixture.h`\n"
        "// (fixed randomness, dummy values only). LiveSyncCryptoTest opens it with the firmware's code, and\n"
        "// test/test_livesync_setup_uri.py checks this file is what the script still writes.\n"
        "// clang-format off\n"
        "namespace pyfixture {\n"
        "constexpr const char* PASSPHRASE = %s;\n"
        "constexpr const char* SETUP_URI =\n%s;\n"
        "constexpr const char* SETTINGS_JSON =\n%s;\n"
        "}  // namespace pyfixture\n"
        "// clang-format on\n"
    ) % (json.dumps(FIXTURE_PASSPHRASE), literal(uri), literal(settings))


# --- Command line -------------------------------------------------------------------------------


def _read_secret(path, env_name, prompt, hidden=True, confirm=False):
    if path:
        try:
            with open(path, "r", encoding="utf-8") as handle:
                return handle.read().rstrip("\r\n")
        except UnicodeDecodeError:
            # Without the bytes themselves: the file may hold a passphrase.
            raise SetupUriError("%s is not UTF-8 text." % path) from None
    if os.environ.get(env_name):
        value = os.environ[env_name]
        try:
            value.encode("utf-8")
        except UnicodeEncodeError:
            raise SetupUriError("$%s is not UTF-8 text." % env_name) from None
        return value
    if not sys.stdin.isatty():
        raise SetupUriError("%s: give a file, set $%s, or run in a terminal to be asked." % (prompt, env_name))
    ask = getpass.getpass if hidden else input
    value = ask(prompt + ": ")
    if confirm and ask(prompt + " (again): ") != value:
        raise SetupUriError("The two entries differ.")
    return value


def write_private(path, text, force=False):
    """Writes `text` to a new file only its owner can read (mode 600).

    The file is created with O_EXCL, which never follows a symbolic link and never opens a file
    that is already there (one that others may read, or a link placed by someone else). With
    `force` an existing entry is removed first: the link itself, never the file it points at.
    """
    if force:
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags, 0o600)
    except FileExistsError:
        raise SetupUriError("%s already exists: choose another name, or pass --force to replace it." % path) from None
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        if hasattr(os, "fchmod"):
            os.fchmod(descriptor, 0o600)  # whatever the umask says
        handle.write(text)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Re-encrypt an Obsidian LiveSync Setup URI with a new passphrase.",
        epilog="Secrets come from files, the environment or a prompt, never from the command line.",
        allow_abbrev=False,
    )
    parser.add_argument("--uri-file", help="file holding the Setup URI (else $LIVESYNC_SETUP_URI, else a prompt)")
    parser.add_argument("--passphrase-file", help="file holding the URI's passphrase (else $LIVESYNC_SETUP_PASSPHRASE)")
    parser.add_argument("--new-passphrase-file", help="file holding the new passphrase (else $LIVESYNC_NEW_PASSPHRASE)")
    parser.add_argument("--out", help="write the new Setup URI to this new file (mode 600) instead of standard output")
    parser.add_argument("--force", action="store_true", help="with --out: replace the file when it already exists")
    parser.add_argument("--self-test", action="store_true", help="check the cipher and a dummy URI round trip, then exit")
    parser.add_argument("--write-fixture", metavar="HEADER", help="write the C++ test fixture (dummy values), then exit")
    args = parser.parse_args(argv)

    if args.self_test:
        self_test()
        print("self-test passed")
        return 0
    if args.write_fixture:
        with open(args.write_fixture, "w", encoding="utf-8") as handle:
            handle.write(fixture_header())
        return 0
    try:
        uri = _read_secret(args.uri_file, "LIVESYNC_SETUP_URI", "Setup URI", hidden=False)
        old = _read_secret(args.passphrase_file, "LIVESYNC_SETUP_PASSPHRASE", "Passphrase of this Setup URI")
        new = _read_secret(args.new_passphrase_file, "LIVESYNC_NEW_PASSPHRASE", "New passphrase", confirm=True)
        if not new:
            raise SetupUriError("The new passphrase is empty.")
        fresh = encode_setup_uri(decode_setup_uri(uri, old), new)
        # Never hand out a URI that doesn't open with the new passphrase.
        if decode_setup_uri(fresh, new) != decode_setup_uri(uri, old):
            raise SetupUriError("The new URI did not read back; nothing was written.")
        if args.out:
            write_private(args.out, fresh + "\n", force=args.force)
    except (SetupUriError, OSError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    except UnicodeError:
        # A typed or piped value that isn't UTF-8. The error's own text quotes the input: not shown.
        print("error: the input is not UTF-8 text.", file=sys.stderr)
        return 1
    if args.out:
        print("New Setup URI written to %s" % args.out, file=sys.stderr)
    else:
        print(fresh)
    return 0


if __name__ == "__main__":
    sys.exit(main())
