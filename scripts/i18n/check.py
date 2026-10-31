#!/usr/bin/env python3
"""Checks a language catalog against translations/template.json.

    python3 scripts/i18n/check.py translations/de.json [--allow-missing]

Every string: present and non-empty, the same {} placeholders in the same
order, the same number of line breaks, the same leading/trailing spaces, no
Russian left in a language written in another script. Exit code 1 on errors.
"""
import json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CYRILLIC_LANGS = {"ru", "uk", "be", "kk"}
HOLE = re.compile(r"\{[^{}]*\}")
CYR = re.compile(r"[А-Яа-яЁё]")
# Words that stay as they are in every language: standards and product names.
KEEP_RUSSIAN = ()


def check(path, allow_missing=False):
    template = json.loads((ROOT / "translations/template.json").read_text(encoding="utf-8"))["strings"]
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    errors = []
    for field in ("code", "name", "strings"):
        if field not in data:
            errors.append(f"no '{field}'")
    code, strings = data.get("code", ""), data.get("strings", {})
    if code in ("", "xx"):
        errors.append("code is not set")
    for key in strings:
        if key not in template:
            errors.append(f"unknown key: {key!r}")
    missing = 0
    for key in template:
        value = strings.get(key, "")
        if not value:
            missing += 1
            if not allow_missing:
                errors.append(f"missing: {key!r}")
            continue
        if HOLE.findall(key) != HOLE.findall(value):
            errors.append(f"placeholders {HOLE.findall(key)} -> {HOLE.findall(value)}: {key!r} -> {value!r}")
        if key.count("\n") != value.count("\n"):
            errors.append(f"line breaks {key.count(chr(10))} -> {value.count(chr(10))}: {key!r} -> {value!r}")
        for side, a, b in (("leading", key[: len(key) - len(key.lstrip())], value[: len(value) - len(value.lstrip())]),
                           ("trailing", key[len(key.rstrip()):], value[len(value.rstrip()):])):
            if a != b:
                errors.append(f"{side} spaces differ: {key!r} -> {value!r}")
        if code not in CYRILLIC_LANGS:
            rest = value
            for word in KEEP_RUSSIAN:
                rest = rest.replace(word, "")
            if CYR.search(rest):
                errors.append(f"Russian left: {key!r} -> {value!r}")
    return errors, missing, len(template)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    allow = "--allow-missing" in sys.argv
    bad = False
    for p in args:
        errors, missing, total = check(p, allow)
        print(f"{p}: {total - missing}/{total} translated, {len(errors)} problems")
        for e in errors[:60]:
            print("  " + e)
        bad = bad or bool(errors)
    sys.exit(1 if bad else 0)
