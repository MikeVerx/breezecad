#!/usr/bin/env python3
"""Collects the interface strings of BreezeCAD for translators.

    extract.py                      writes translations/template.json
    extract.py --update FILE.json   adds new strings to a language file (empty),
                                    drops the ones no longer used, reports both

Russian is the source language: a string's Russian text is its key. In the
interface code (src/app, src/ui) the strings are the arguments of Tr(), Trf()
and N_(). The lower modules (document, kernel, core...) produce messages that
the interface translates when it shows them; every Russian literal there is
taken, and std::format patterns keep their "{}" holes, which the program
matches against the text at run time ("Удалить {}" -> "Delete {}").
"""
import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
UI_DIRS = ['src/app', 'src/ui']
LOWER_DIRS = ['src/document', 'src/kernel', 'src/core', 'src/sketch', 'src/platform']
CYR = re.compile(r'[А-Яа-яЁё]')
LITERAL = r'"(?:[^"\\\n]|\\.)*"'
RUN = re.compile(r'{0}(?:\s*{0})*'.format(LITERAL))
MARKED = re.compile(r'\b(?:Tr|Trf|N_)\(\s*({0}(?:\s*{0})*)'.format(LITERAL))


def strip_comments(text):
    """Blanks out comments, keeping string literals and line breaks."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            m = re.compile(LITERAL).match(text, i)
            end = m.end() if m else i + 1
            out.append(text[i:end])
            i = end
        elif c == "'":
            m = re.compile(r"'(?:[^'\\\n]|\\.)*'").match(text, i)
            end = m.end() if m else i + 1
            out.append(text[i:end])
            i = end
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            chunk = text[i:n if j < 0 else j + 2]
            out.append('\n' * chunk.count('\n'))
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def decode(run):
    """The text of adjacent C++ literals (UTF-8 escapes included)."""
    data = bytearray()
    for lit in re.findall(LITERAL, run):
        body, i = lit[1:-1], 0
        while i < len(body):
            c = body[i]
            if c != '\\':
                data += c.encode('utf-8')
                i += 1
                continue
            e = body[i + 1]
            if e == 'x':
                m = re.match(r'[0-9A-Fa-f]{1,2}', body[i + 2:])
                data.append(int(m.group(0), 16))
                i += 2 + m.end()
            else:
                data += {'n': b'\n', 't': b'\t', '"': b'"', '\\': b'\\', "'": b"'", '0': b'\0'}.get(e, e.encode())
                i += 2
    return data.decode('utf-8')


def key_of(text):
    text = text.split('##', 1)[0] if '##' in text[1:] else text   # "Готово##id": the id is not translated
    return text if CYR.search(text) else None


def sources(dirs):
    for d in dirs:
        for path in sorted((ROOT / d).rglob('*')):
            if path.suffix in ('.h', '.cpp'):
                yield path


def collect():
    keys = {}
    for path in sources(UI_DIRS):
        text = strip_comments(path.read_text(encoding='utf-8'))
        for m in MARKED.finditer(text):
            k = key_of(decode(m.group(1)))
            if k:
                keys.setdefault(k, f'{path.relative_to(ROOT)}:{text.count(chr(10), 0, m.start()) + 1}')
    for path in sources(LOWER_DIRS):
        if path.name.startswith('I18n'):
            continue
        text = strip_comments(path.read_text(encoding='utf-8'))
        for m in RUN.finditer(text):
            k = key_of(decode(m.group(0)))
            if k:
                keys.setdefault(k, f'{path.relative_to(ROOT)}:{text.count(chr(10), 0, m.start()) + 1}')
    return keys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--update', type=pathlib.Path, help='language file to bring up to date')
    args = ap.parse_args()
    keys = collect()

    if args.update:
        data = json.loads(args.update.read_text(encoding='utf-8'))
        old = data.get('strings', {})
        added = [k for k in keys if k not in old]
        dropped = [k for k in old if k not in keys]
        data['strings'] = {k: old.get(k, '') for k in sorted(keys)}
        args.update.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        done = sum(1 for v in data['strings'].values() if v)
        print(f'{args.update}: {done} of {len(keys)} translated; {len(added)} new, {len(dropped)} dropped')
        for k in dropped:
            print('  dropped:', k.replace('\n', '\\n'))
        return 0

    out = ROOT / 'translations' / 'template.json'
    out.parent.mkdir(exist_ok=True)
    data = {'code': 'xx', 'name': 'Language name, in that language', 'strings': {k: '' for k in sorted(keys)}}
    out.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'{out.relative_to(ROOT)}: {len(keys)} strings')
    return 0


if __name__ == '__main__':
    sys.exit(main())
