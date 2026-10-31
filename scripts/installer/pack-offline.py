#!/usr/bin/env python3
"""The offline installer: BreezeCAD-Setup.exe with everything packed in.

The manifest and the files it names go into a ZIP (stored: they are
compressed already) appended to the .exe, then the trailer the installer
looks for at its end: b"BZSETUP1" + the ZIP's offset (8 bytes, little
endian). Nothing is downloaded by such an installer (Payload.cpp).

    pack-offline.py BreezeCAD-Setup.exe setup-manifest.json <folder with the files> -o BreezeCAD-Setup-offline.exe
"""
import argparse
import io
import json
import os
import struct
import zipfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("setup")
    ap.add_argument("manifest")
    ap.add_argument("folders", nargs="+", help="where the manifest's files are")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    manifest = json.load(open(a.manifest, encoding="utf-8"))
    names = [manifest["program"]["file"]] + [m["file"] for m in manifest["modules"]] + [l["file"] for l in manifest["languages"]]

    def find(name):
        for folder in a.folders:
            p = os.path.join(folder, name)
            if os.path.isfile(p):
                return p
        raise SystemExit(f"not found: {name}")

    payload = io.BytesIO()
    with zipfile.ZipFile(payload, "w", zipfile.ZIP_STORED) as z:
        z.write(a.manifest, "setup-manifest.json")
        for n in names:
            z.write(find(n), n)
    exe = open(a.setup, "rb").read()
    with open(a.out, "wb") as f:
        f.write(exe)
        f.write(payload.getvalue())
        f.write(b"BZSETUP1" + struct.pack("<Q", len(exe)))
    print(f"{a.out}: {os.path.getsize(a.out) / 1048576:.1f} MB ({len(names)} files inside)")


if __name__ == "__main__":
    main()
