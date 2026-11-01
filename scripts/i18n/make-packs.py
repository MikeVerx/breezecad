#!/usr/bin/env python3
"""Language packs for the installer: translations/<code>.json -> <out>/lang-<code>.zip.

    python3 scripts/i18n/make-packs.py release

Each pack holds <code>.json (and <code>.ttf/.otf when translations/fonts/ has
one); the installer unpacks it into the profile's lang/ folder. English is
built into the program (assets/lang/en.json) and gets no pack. A catalog that
fails scripts/i18n/check.py is not packed: a broken pack must not ship.
"""
import os, sys, zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check import check  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def main(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    made, bad = [], []
    for catalog in sorted((ROOT / "translations").glob("*.json")):
        code = catalog.stem
        if code in ("template", "en", "ru"):
            continue
        errors, missing, total = check(catalog, allow_missing=True)
        if errors:
            bad.append(code)
            print(f"{catalog.name}: {len(errors)} problems, not packed", file=sys.stderr)
            continue
        pack = out / f"lang-{code}.zip"
        with zipfile.ZipFile(pack, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(catalog, f"{code}.json")
            for ext in (".ttf", ".otf"):
                font = ROOT / "translations/fonts" / f"{code}{ext}"
                if font.exists():
                    z.write(font, font.name)
        made.append(pack)
        print(f"{pack.name}: {total - missing}/{total} strings")
    if bad:
        sys.exit(1)
    return made


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "release")
