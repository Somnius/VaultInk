#!/usr/bin/env python3
"""Tests for the open-source licence list (docs/third-party.json and scripts/gen_licenses.py).

    python3 test/test_licenses.py
"""

import os
import re
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import gen_licenses  # noqa: E402


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


class GeneratedFiles(unittest.TestCase):
    """The checked-in outputs are what the script writes from the JSON today."""

    def setUp(self):
        self.data = gen_licenses.load()

    def test_licenses_md_is_up_to_date(self):
        self.assertEqual(read("docs/licenses.md"), gen_licenses.render_md(self.data))

    def test_header_is_up_to_date(self):
        self.assertEqual(read("src/util/ThirdPartyData.h"), gen_licenses.render_header(self.data))

    def test_notice_is_up_to_date(self):
        notice = read("NOTICE")
        self.assertEqual(notice, gen_licenses.render_notice(self.data, notice))

    def test_check_mode_passes(self):
        self.assertEqual(gen_licenses.main(["--check"]), 0)

    def test_notice_keeps_its_statements(self):
        notice = read("NOTICE")
        self.assertIn("VaultInk is a fork of CrossPoint Reader", notice)
        self.assertIn("Copyright (c) 2025 CrossPoint Reader organization, MIT License.", notice)
        self.assertIn("FreeInk SDK", notice)
        self.assertIn("wolfSSL is dual-licensed (GPL or a commercial licence). VaultInk uses it under the GPL.", notice)
        self.assertIn("the firmware binaries are distributed under the terms of the\n  GPL-3.0-or-later", notice)
        self.assertIn("wolfSSH 1.6.0", notice)
        for c in self.data["components"]:
            self.assertIn(f"- {c['name']}", notice)


class Data(unittest.TestCase):
    def setUp(self):
        self.data = gen_licenses.load()
        self.components = self.data["components"]

    def by_name(self, name):
        return next(c for c in self.components if c["name"] == name)

    def test_every_group_has_entries(self):
        for key, _ in gen_licenses.GROUPS:
            self.assertTrue(gen_licenses.in_group(self.data, key), key)

    def test_no_licence_without_a_file_it_was_read_from(self):
        for c in self.components:
            if c["licence"] == gen_licenses.NOT_VERIFIED:
                self.assertEqual(c["verified_from"], "", c["name"])
                self.assertTrue(c.get("notes"), f"{c['name']}: say why it is not verified")
            else:
                self.assertTrue(c["verified_from"], c["name"])

    def test_files_in_the_repository_exist(self):
        # Paths the build tools download, and the submodule, are not in a plain checkout.
        outside = (".pio/", "~/", "freeink-sdk/")
        checked = 0
        for c in self.components:
            path = c["verified_from"].split(" ")[0].rstrip(",")
            if not path or path.startswith(outside):
                continue
            self.assertTrue(os.path.exists(os.path.join(ROOT, path)), f"{c['name']}: {path}")
            checked += 1
        self.assertGreater(checked, 10)

    def test_wolfssl_is_gpl(self):
        self.assertEqual(self.by_name("wolfSSL")["licence"], "GPL-2.0-or-later")

    def test_wolfssh_is_gpl3_and_vendored(self):
        ssh = self.by_name("wolfSSH")
        self.assertEqual(ssh["licence"], "GPL-3.0-or-later")
        self.assertEqual(ssh["only_with"], "VAULTINK_SSH")
        # The licence was read from the vendored files, and they say what the JSON says.
        licensing = read("lib/wolfssh/LICENSING")
        self.assertIn("licensed for use under the GPLv3 or a standard commercial", licensing)
        header = read("lib/wolfssh/src/ssh.c")[:1200]
        self.assertIn("either version 3 of the License, or", header)
        self.assertIn("(at your option) any later version", header)
        self.assertIn("GNU GENERAL PUBLIC LICENSE", read("lib/wolfssh/COPYING")[:200])
        self.assertIn("Version 3, 29 June 2007", read("lib/wolfssh/COPYING")[:200])
        # The version in the list is the vendored one, and the note names the same commit.
        version = re.search(r'LIBWOLFSSH_VERSION_STRING "([0-9.]+)"', read("lib/wolfssh/wolfssh/version.h")).group(1)
        self.assertEqual(gen_licenses.short_version(ssh["version"]), version)
        upstream = read("lib/wolfssh/UPSTREAM.md")
        self.assertIn(f"Version: {version}", upstream)
        self.assertIn(re.search(r"commit ([0-9a-f]{8})", ssh["version"]).group(1), upstream)
        # The flag that links it is set where the licence text says it is: the X4 Pro builds.
        ini = read("platformio.ini")
        self.assertIn("-DVAULTINK_SSH=1", ini)
        self.assertEqual(ini.count("${vaultink_ssh.build_flags}"), 3)

    def test_no_gpl2_only_component(self):
        # A GPL-2.0-only component could not be in a GPL-3.0-or-later binary.
        for c in self.components:
            self.assertNotIn("GPL-2.0-only", c["licence"], c["name"])

    def test_pinned_versions_match_platformio_ini(self):
        ini = read("platformio.ini")
        for name, spec in (
            ("ArduinoJson", "bblanchon/ArduinoJson @ "),
            ("SdFat", "greiman/SdFat @ "),
            ("QRCode", "ricmoo/QRCode @ "),
            ("PNGdec", "bitbank2/PNGdec @ "),
            ("WebSockets", "links2004/WebSockets @ "),
            ("wolfSSL", "wolfssl/Arduino-wolfSSL @ "),
            ("NimBLE-Arduino", "h2zero/NimBLE-Arduino@"),
        ):
            version = gen_licenses.short_version(self.by_name(name)["version"])
            self.assertIn(spec + version + "\n", ini, name)
        for name, repo in (("JPEGDEC", "JPEGDEC.git#"), ("AnimatedGIF", "AnimatedGIF.git#")):
            commit = re.search(r"commit ([0-9a-f]{8})", self.by_name(name)["version"]).group(1)
            self.assertIn(repo + commit, ini, name)

    def test_companion_versions_match_gradle(self):
        gradle = read("companion/app/build.gradle.kts")
        for name, spec in (
            ("OkHttp", "com.squareup.okhttp3:okhttp:"),
            ("AndroidX WebKit", "androidx.webkit:webkit:"),
            ("AndroidX Activity Compose", "androidx.activity:activity-compose:"),
            ("kotlinx.coroutines", "org.jetbrains.kotlinx:kotlinx-coroutines-android:"),
            ("JUnit", "junit:junit:"),
        ):
            version = gen_licenses.short_version(self.by_name(name)["version"])
            self.assertIn(f'"{spec}{version}"', gradle, name)

    def test_load_refuses_a_licence_nobody_read(self):
        import json
        import tempfile

        bad = {"components": [dict(self.by_name("Expat"), verified_from="")]}
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump(bad, f)
        try:
            with self.assertRaises(ValueError):
                gen_licenses.load(f.name)
        finally:
            os.unlink(f.name)


class Device(unittest.TestCase):
    def setUp(self):
        self.data = gen_licenses.load()
        self.rows = gen_licenses.device_rows(self.data)

    def test_strings_fit_the_flash_budget(self):
        size = gen_licenses.device_string_bytes(self.rows)
        self.assertLessEqual(size, gen_licenses.DEVICE_STRING_BUDGET)
        self.assertIn(f"STRING_BYTES = {size};", read("src/util/ThirdPartyData.h"))
        # Strings plus the table (five 4-byte pointers a row) stay under 6 KB too.
        self.assertLess(size + 20 * len(self.rows), 6 * 1024)

    def test_only_what_is_in_the_binary(self):
        names = [row[0] for row in self.rows]
        self.assertTrue(any(n.startswith("wolfSSL") for n in names))
        self.assertTrue(any(n.startswith("wolfSSH") for n in names))
        self.assertTrue(any(n.startswith("JSZip") for n in names))
        for absent in ("OkHttp", "ESP Web Tools", "pioarduino"):
            self.assertFalse(any(n.startswith(absent) for n in names), absent)

    def test_strings_are_plain_ascii(self):
        # The UI fonts are not asked to draw anything outside ASCII here.
        for row in self.rows:
            for text in row:
                self.assertTrue(text.isascii(), text)
                self.assertLess(len(text), 180, text)  # LicencesActivity's line buffer is 224 bytes

    def test_ui_strings_exist(self):
        for lang in ("english", "greek"):
            yaml = read(f"lib/I18n/translations/{lang}.yaml")
            for key in (
                "STR_LICENCES",
                "STR_LICENCES_INTRO",
                "STR_LICENCE_FIELD",
                "STR_LICENCE_USED_FOR",
                "STR_LICENCE_SOURCE",
                "STR_LICENCE_COPYRIGHT",
            ):
                self.assertEqual(len(re.findall(rf"^{key}: \".+\"$", yaml, re.M)), 1, f"{lang} {key}")
        english = read("lib/I18n/translations/english.yaml")
        intro = re.search(r'^STR_LICENCES_INTRO: "(.+)"$', english, re.M).group(1)
        self.assertEqual(
            intro,
            "VaultInk is MIT. The firmware binary also holds wolfSSL (GPL-2.0-or-later) and Apache-2.0 code, so it "
            "is distributed under GPL-3.0-or-later. Source: github.com/Somnius/VaultInk",
        )
        # The header line of a build with the SSH library (the shipped X4 Pro build).
        intro_ssh = re.search(r'^STR_LICENCES_INTRO_SSH: "(.+)"$', english, re.M).group(1)
        self.assertEqual(
            intro_ssh,
            "VaultInk is MIT. The firmware binary includes wolfSSL (GPL-2.0-or-later) and wolfSSH "
            "(GPL-3.0-or-later), so it is distributed under the GPL-3.0-or-later. Source: github.com/Somnius/VaultInk",
        )
        self.assertEqual(len(re.findall(r'^STR_LICENCES_INTRO_SSH: ".+"$', read("lib/I18n/translations/greek.yaml"), re.M)), 1)
        activity = read("src/activities/settings/LicencesActivity.cpp")
        self.assertIn("STR_LICENCES_INTRO_SSH", activity)
        self.assertIn("VAULTINK_SSH", activity)

    def test_ssh_row_is_only_in_ssh_builds(self):
        header = read("src/util/ThirdPartyData.h")
        self.assertRegex(header, r'#if defined\(VAULTINK_SSH\) && VAULTINK_SSH\n    \{"wolfSSH [0-9.]+", "GPL-3.0-or-later",[^\n]+\n#endif\n')
        self.assertEqual(header.count("#if defined("), 1)


class OtherPlaces(unittest.TestCase):
    def setUp(self):
        self.data = gen_licenses.load()

    def test_companion_about_lists_the_same_libraries(self):
        kotlin = read("companion/app/src/main/java/com/lefteros/vaultink/AboutScreen.kt")
        listed = re.findall(r'^\s+"([^"]+)" to "([^"]+)",$', kotlin, re.M)
        shipped = [
            (c["name"], c["licence"])
            for c in gen_licenses.in_group(self.data, "companion")
            if not c.get("build_only")
        ]
        self.assertEqual(listed, shipped)

    def test_site_and_readme_link_the_list(self):
        site = read("site/index.html")
        self.assertIn("https://github.com/Somnius/VaultInk/blob/main/docs/licenses.md", site)
        self.assertIn("wolfSSL: GPL-2.0-or-later", site)
        self.assertIn("wolfSSH: GPL-3.0-or-later", site)
        self.assertIn("distributed under the GPL-3.0-or-later", site)
        self.assertNotIn("is distributed under the GPL terms", site)
        readme = read("README.md")
        self.assertIn("so the binaries are distributed under the GPL-3.0-or-later", readme)
        self.assertNotIn("so the binaries are distributed under the GPL terms", readme)
        self.assertIn("(docs/licenses.md)", read("README.md"))
        self.assertIn("(licenses.md)", read("docs/README.md"))


if __name__ == "__main__":
    unittest.main()
