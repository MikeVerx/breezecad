#!/usr/bin/env python3
"""The Linux package as the installer takes it: BreezeCAD-linux.tar.gz -> .zip
with the Unix permissions kept (the program and its libraries stay executable).

    python3 scripts/installer/tar2zip.py release/BreezeCAD-linux.tar.gz release/BreezeCAD-linux.zip
"""
import sys, tarfile, zipfile

src, out = sys.argv[1], sys.argv[2]
with tarfile.open(src, "r:gz") as tar, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for m in tar.getmembers():
        if m.isdir() or not (m.isfile() or m.issym()):
            continue
        if m.issym():
            raise SystemExit(f"tar2zip: a symbolic link is not supported: {m.name}")
        info = zipfile.ZipInfo(m.name, date_time=(2026, 1, 1, 0, 0, 0))
        info.external_attr = (0o100000 | (m.mode & 0o777)) << 16
        info.create_system = 3   # Unix: the permissions are read from external_attr
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, tar.extractfile(m).read())
print(f"tar2zip: {out}")
