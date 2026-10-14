#!/usr/bin/env python3
"""The text of the release page "modules": every module, what it does and
its download for each system, made from the module.json inside each package.

    python3 scripts/modules-page.py <release tag> <folder with .bzmod files> > notes.md

The links point at the release's files (GITHUB_REPOSITORY, or MikeVerx/breezecad).
A module with a README in modules/<id>/ gets a link to it.
"""
import json
import os
import sys
import zipfile
from pathlib import Path

SYSTEMS = [("windows", "Windows 10/11 (64-бит)"), ("linux", "Linux (x86_64)"), ("macos", "macOS 14+ (Apple M1 и новее)")]


def manifest(package: Path) -> dict:
    """module.json of a package: at the top or in its only folder."""
    with zipfile.ZipFile(package) as z:
        names = [n for n in z.namelist() if n == "module.json" or (n.endswith("/module.json") and n.count("/") == 1)]
        if not names:
            raise SystemExit(f"{package.name}: в пакете нет module.json")
        return json.loads(z.read(sorted(names, key=len)[0]).decode("utf-8"))


def system_of(name: str) -> str:
    for system, _ in SYSTEMS:
        if f"-{system}-" in name:
            return system
    return "other"


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    tag, folder = sys.argv[1], Path(sys.argv[2])
    repo = os.environ.get("GITHUB_REPOSITORY", "MikeVerx/breezecad")
    root = Path(__file__).resolve().parent.parent

    modules: dict[str, dict] = {}
    for package in sorted(folder.glob("*.bzmod")):
        m = manifest(package)
        entry = modules.setdefault(m["id"], {"manifest": m, "files": {}})
        entry["files"][system_of(package.name)] = package.name

    out = [
        "Модули добавляют BreezeCAD новые возможности. Программа их в себе не несёт: "
        "скачайте только нужные — файл `.bzmod` для своей системы.",
        "",
        "**Как поставить:** BreezeCAD → **Настройки → Модули → «Установить модуль…»** "
        "или просто перетащите файл `.bzmod` в окно программы. Модуль заработает сразу; "
        "удаляется там же.",
        "",
        f"Сама программа — на странице [последнего выпуска](https://github.com/{repo}/releases/latest).",
        "",
    ]
    for module_id, entry in sorted(modules.items(), key=lambda kv: kv[1]["manifest"].get("name", kv[0])):
        m = entry["manifest"]
        out += [f"## {m.get('name', module_id)} — {m.get('version', '')}", "", m.get("description", ""), ""]
        need = m.get("requirements", {}).get("minHostVersion")
        if need:
            out += [f"Нужен BreezeCAD {need} или новее.", ""]
        out += ["| Система | Файл |", "| --- | --- |"]
        for system, title in SYSTEMS + [("other", "Другая")]:
            name = entry["files"].get(system)
            if name:
                out.append(f"| {title} | [{name}](https://github.com/{repo}/releases/download/{tag}/{name}) |")
        if (root / "modules" / module_id / "README.md").is_file():
            out += ["", f"Подробно, как подключить и пользоваться, — [README модуля](https://github.com/{repo}/blob/next/modules/{module_id}/README.md)."]
        out.append("")
    print("\n".join(out).rstrip() + "\n")


if __name__ == "__main__":
    main()
