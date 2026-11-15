#!/usr/bin/env python3
"""Tests for scripts/livesync_setup_uri.py (dummy values only).

    python3 test/test_livesync_setup_uri.py
"""

import contextlib
import io
import json
import os
import re
import stat
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))

import livesync_setup_uri as lsu  # noqa: E402


def c_string(header, name):
    """A `constexpr const char* NAME = "..." "...";` from a C++ header, as bytes."""
    match = re.search(r"constexpr const char\* %s =(.*?);\n" % re.escape(name), header, re.S)
    if not match:
        raise AssertionError("%s not found" % name)
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(1))
    return b"".join(part.encode("utf-8").decode("unicode_escape").encode("latin-1") for part in parts)


class SetupUriTests(unittest.TestCase):
    def test_self_test(self):
        self.assertTrue(lsu.self_test())

    def test_opens_a_uri_the_real_library_made(self):
        # test/livesync/vectors.h comes from the plugin's own library; the firmware's tests open
        # the same URI (LiveSyncCryptoTest, DecodesSetupUri).
        with open(os.path.join(HERE, "livesync", "vectors.h"), encoding="utf-8") as handle:
            header = handle.read()
        passphrase = c_string(header, "URI_PASSPHRASE").decode()
        settings = c_string(header, "SETTINGS_JSON")
        self.assertEqual(lsu.decode_setup_uri(c_string(header, "SETUP_URI").decode(), passphrase), settings)
        self.assertEqual(lsu.decode_setup_uri(c_string(header, "SETUP_BLOB").decode(), passphrase), settings)
        with self.assertRaises(lsu.SetupUriError):
            lsu.decode_setup_uri(c_string(header, "SETUP_URI").decode(), "wrong")

    def test_fixture_for_the_cpp_test_is_current(self):
        # LiveSyncCryptoTest opens this file's URI with the firmware's code.
        with open(os.path.join(HERE, "livesync", "python_uri_fixture.h"), encoding="utf-8") as handle:
            committed = handle.read()
        self.assertEqual(committed, lsu.fixture_header())
        uri = c_string(committed, "SETUP_URI").decode()
        settings = lsu.decode_setup_uri(uri, c_string(committed, "PASSPHRASE").decode())
        self.assertEqual(settings, c_string(committed, "SETTINGS_JSON"))
        self.assertEqual(json.loads(settings)["couchDB_DBNAME"], "dummy-vault")

    def test_uri_shape(self):
        uri = lsu.encode_setup_uri(b'{"a":1}', "dummy")
        self.assertTrue(uri.startswith("obsidian://setuplivesync?settings=%25%24"))
        self.assertNotRegex(uri[len(lsu.PREFIX):], r"[+/= ]")  # all escaped, as encodeURIComponent does
        box_len = lsu.PBKDF2_SALT_LEN + lsu.IV_LEN + lsu.HKDF_SALT_LEN + len(b'{"a":1}') + lsu.TAG_LEN
        self.assertEqual(len(lsu.blob_of(uri)) - 2, (box_len + 2) // 3 * 4)
        self.assertNotEqual(uri, lsu.encode_setup_uri(b'{"a":1}', "dummy"))  # fresh salts and IV each time

    def test_damaged_input(self):
        uri = lsu.encode_setup_uri(b'{"a":1}', "dummy")
        flipped = uri[:-6] + ("A" if uri[-6] != "A" else "B") + uri[-5:]
        with self.assertRaises(lsu.SetupUriError):
            lsu.decode_setup_uri(flipped, "dummy")
        for bad in ("", "https://example.org", "%$AAAA", "%$not base64!"):
            with self.assertRaises(lsu.SetupUriError):
                lsu.decode_setup_uri(bad, "dummy")

    def test_reencrypts_from_files_without_printing_secrets(self):
        settings = json.dumps(lsu.DUMMY_SETTINGS).encode()
        with tempfile.TemporaryDirectory() as folder:
            paths = {name: os.path.join(folder, name) for name in ("uri", "old", "new", "out")}
            with open(paths["uri"], "w") as handle:
                handle.write(lsu.encode_setup_uri(settings, "dummy old passphrase") + " \n")
            with open(paths["old"], "w") as handle:
                handle.write("dummy old passphrase\n")
            with open(paths["new"], "w") as handle:
                handle.write("dummy-new\n")
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = lsu.main(["--uri-file", paths["uri"], "--passphrase-file", paths["old"],
                                 "--new-passphrase-file", paths["new"], "--out", paths["out"]])
            self.assertEqual(code, 0)
            with open(paths["out"]) as handle:
                fresh = handle.read().strip()
            self.assertEqual(lsu.decode_setup_uri(fresh, "dummy-new"), settings)
            with self.assertRaises(lsu.SetupUriError):
                lsu.decode_setup_uri(fresh, "dummy old passphrase")
            self.assertEqual(stat.S_IMODE(os.stat(paths["out"]).st_mode), 0o600)
            printed = out.getvalue() + err.getvalue()
            for secret in ("dummy old passphrase", "dummy-new", "dummy-password", "dummy-e2ee-passphrase"):
                self.assertNotIn(secret, printed)

            # A wrong passphrase: an error, no output file.
            with open(paths["old"], "w") as handle:
                handle.write("not it\n")
            os.remove(paths["out"])
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = lsu.main(["--uri-file", paths["uri"], "--passphrase-file", paths["old"],
                                 "--new-passphrase-file", paths["new"], "--out", paths["out"]])
            self.assertEqual(code, 1)
            self.assertFalse(os.path.exists(paths["out"]))

    def test_reads_the_environment_and_writes_to_stdout(self):
        settings = b'{"couchDB_DBNAME":"dummy-vault"}'
        env = {"LIVESYNC_SETUP_URI": lsu.encode_setup_uri(settings, "dummy-a"),
               "LIVESYNC_SETUP_PASSPHRASE": "dummy-a", "LIVESYNC_NEW_PASSPHRASE": "dummy-b"}
        out = io.StringIO()
        with mock.patch.dict(os.environ, env), contextlib.redirect_stdout(out):
            self.assertEqual(lsu.main([]), 0)
        self.assertEqual(lsu.decode_setup_uri(out.getvalue().strip(), "dummy-b"), settings)

    def test_no_passphrase_option_on_the_command_line(self):
        # Passphrases and the URI come from files, the environment or a prompt: there is no option
        # that takes one as its value.
        with contextlib.redirect_stderr(io.StringIO()):
            for option in ("--passphrase", "--new-passphrase", "--uri"):
                with self.assertRaises(SystemExit):
                    lsu.main([option, "dummy"])


if __name__ == "__main__":
    unittest.main()
