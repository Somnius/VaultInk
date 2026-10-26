#!/usr/bin/env python3
"""Show how complete each UI translation is, or list what one language is missing.

  python3 scripts/i18n_status.py            # one line per language
  python3 scripts/i18n_status.py greek      # the English keys greek.yaml lacks, with their text

Missing keys are not an error: the device shows the English text for them.
"""
import pathlib
import re
import sys

TRANSLATIONS = pathlib.Path(__file__).resolve().parent.parent / "lib" / "I18n" / "translations"
LINE = re.compile(r'^(STR_[A-Z0-9_]+):\s*"(.*)"\s*$')
PLACEHOLDER = re.compile(r"%[0-9.]*[sdu%]")


def load(path):
    entries = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = LINE.match(line)
        if match:
            entries[match.group(1)] = match.group(2)
    return entries


def main():
    english = load(TRANSLATIONS / "english.yaml")
    if len(sys.argv) > 1:
        path = TRANSLATIONS / f"{sys.argv[1]}.yaml"
        if not path.exists():
            sys.exit(f"no such file: {path}")
        other = load(path)
        for key, text in english.items():
            if key not in other:
                print(f'{key}: "{text}"')
            elif sorted(PLACEHOLDER.findall(text)) != sorted(PLACEHOLDER.findall(other[key])):
                print(f"# placeholders differ from English: {key}")
        return
    for path in sorted(TRANSLATIONS.glob("*.yaml")):
        if path.name == "english.yaml":
            continue
        other = load(path)
        done = sum(1 for key in english if key in other)
        print(f"{path.stem:24} {done:4}/{len(english)}  {100 * done // len(english):3}%")


if __name__ == "__main__":
    main()
