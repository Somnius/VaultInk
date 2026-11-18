#!/usr/bin/env python3
"""Which builds check GitHub's certificate (platformio.ini, -DVAULTINK_VERIFY_GITHUB=1).

Every X4 Pro build does, the release ones included; no other board does, because none of them has
done the handshake on a device (ROADMAP, Release B gate).

    python3 test/test_verify_github_envs.py
"""

import configparser
import os
import re
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
FLAG = "-DVAULTINK_VERIFY_GITHUB=1"
X4PRO = {
    "x4pro",
    "x4pro-dev",
    "x4pro-dev-verify",
    "x4pro-dev-user",
    "x4pro-gh_release",
    "x4pro-gh_release_rc",
}


def load():
    ini = configparser.ConfigParser(interpolation=None, strict=False)
    ini.read(os.path.join(ROOT, "platformio.ini"), encoding="utf-8")
    return ini


def option(ini, section, key, seen=()):
    """The value PlatformIO would use: the section's own, else the first `extends` parent's."""
    if (section, key) in seen or not ini.has_section(section):
        return ""
    seen = seen + ((section, key),)
    if ini.has_option(section, key):
        raw = ini.get(section, key)
        # ${section.key}; the section name may hold a dot-free "env:name"
        return re.sub(
            r"\$\{([^}.]+)\.([^}]+)\}",
            lambda m: option(ini, m.group(1), m.group(2), seen) if m.group(1) != "sysenv" else "",
            raw,
        )
    if ini.has_option(section, "extends"):
        for parent in re.split(r"[,\s]+", ini.get(section, "extends").strip()):
            if parent and (value := option(ini, parent, key, seen)):
                return value
    return ""


def flags(ini, env):
    text = option(ini, "env:" + env, "build_flags")
    return [line.split(";")[0].strip() for line in text.splitlines()]


class VerifyGithubEnvs(unittest.TestCase):
    def setUp(self):
        self.ini = load()
        self.envs = [s[4:] for s in self.ini.sections() if s.startswith("env:")]

    def test_the_x4pro_envs_exist(self):
        self.assertTrue(X4PRO <= set(self.envs), X4PRO - set(self.envs))

    def test_every_x4pro_build_checks_github_once(self):
        for env in sorted(X4PRO):
            self.assertEqual(flags(self.ini, env).count(FLAG), 1, env)

    def test_no_other_board_checks_github(self):
        for env in self.envs:
            if env in X4PRO:
                continue
            self.assertFalse(any("VAULTINK_VERIFY_GITHUB" in f for f in flags(self.ini, env)), env)

    def test_the_resolver_sees_inherited_flags(self):
        # A flag every device build has through [base]: the resolver follows extends and ${...}.
        for env in ("x4pro-gh_release", "x4pro-dev-verify", "x4c-gh_release"):
            self.assertIn("-DWOLFSSL_SHA384", flags(self.ini, env), env)

    def test_the_default_is_off(self):
        with open(os.path.join(ROOT, "lib/TlsPolicy/TlsPolicy.h"), encoding="utf-8") as f:
            self.assertRegex(f.read(), r"#ifndef VAULTINK_VERIFY_GITHUB\n#define VAULTINK_VERIFY_GITHUB 0\n#endif")


if __name__ == "__main__":
    unittest.main()
