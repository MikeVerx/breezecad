#!/usr/bin/env python3
"""setup-manifest.json for the installer (installer/setup/Payload.h).

What a release offers the installer: the program's zip, the official
modules (.bzmod) and the language packs (lang-<code>.zip), each with its
size, unpacked size and SHA-256 so the installer can check what it got.

    make-manifest.py --version 0.2.5-Beta --tag v0.2.5-beta \\
        --program release/BreezeCAD-windows.zip \\
        --module packages/bz-mcp-1.0.0-windows-x64.bzmod \\
        [--language release/lang-de.zip ...] -o release/setup-manifest.json
"""
import argparse
import hashlib
import json
import os
import zipfile


def describe(path, **extra):
    data = open(path, "rb").read()
    with zipfile.ZipFile(path) as z:
        unpacked = sum(i.file_size for i in z.infolist())
    return {"file": os.path.basename(path), "size": len(data), "unpacked": unpacked,
            "sha256": hashlib.sha256(data).hexdigest(), **extra}


def module(path):
    with zipfile.ZipFile(path) as z:
        names = [n for n in z.namelist() if n == "module.json" or (n.endswith("/module.json") and n.count("/") == 1)]
        meta = json.loads(z.read(names[0]).decode("utf-8")) if names else {}
    return describe(path, id=meta.get("id", os.path.basename(path)), name=meta.get("name", ""),
                    description=meta.get("description", ""))


def language(path):
    code = os.path.basename(path)[len("lang-"):-len(".zip")]
    return describe(path, id=code)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--program", required=True)
    ap.add_argument("--module", action="append", default=[])
    ap.add_argument("--language", action="append", default=[])
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    manifest = {
        "version": a.version,
        "tag": a.tag,
        "program": describe(a.program),
        "modules": [module(m) for m in a.module],
        "languages": [language(l) for l in a.language],
    }
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
    print(f"{a.out}: {manifest['program']['file']}, {len(manifest['modules'])} modules, {len(manifest['languages'])} languages")


if __name__ == "__main__":
    main()
