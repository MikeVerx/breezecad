#!/usr/bin/env python3
"""Damaged files: assemblies and parts with fields mutated at random
(wrong types, missing keys, NaN / huge numbers, ids pointing nowhere),
opened in a running BreezeCAD and worked with a little.

    python3 filefuzz.py SEED COUNT
"""
import copy, json, math, os, random, shutil, sys, time, zipfile
from bz import call

SP = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get("BZ_CRASHTEST", "/tmp/bz-crashtest")   # samples (samples.py) and copies
SAMPLES = os.path.join(WORK, "samples")
LOG = os.environ.get("BZ_LOG", "/tmp/breezecad-ui.log")
seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
count = int(sys.argv[2]) if len(sys.argv) > 2 else 40
rnd = random.Random(seed)
ROOT = os.path.join(WORK, f"filefuzz-{seed}")


def read(path):
    with zipfile.ZipFile(path) as z:
        return {n: z.read(n) for n in z.namelist()}


def write(path, files):
    with zipfile.ZipFile(path, "w") as z:
        for n, b in files.items():
            z.writestr(n, b)


WEIRD = [None, True, -1, 0, 1e308, -1e308, float("nan"), float("inf"), "", "x", [], {}, [1, 2], 2**63, -2**63, 18446744073709551615]


def paths(node, prefix=()):
    yield prefix
    if isinstance(node, dict):
        for k, v in node.items():
            yield from paths(v, prefix + (k,))
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from paths(v, prefix + (i,))


def mutate(doc):
    all_paths = [p for p in paths(doc) if p]
    for _ in range(rnd.randint(1, 4)):
        p = rnd.choice(all_paths)
        parent = doc
        try:
            for k in p[:-1]:
                parent = parent[k]
            key = p[-1]
            r = rnd.random()
            if r < 0.15 and isinstance(parent, dict):
                parent.pop(key, None)
            elif r < 0.3 and isinstance(parent, list) and parent:
                parent.append(copy.deepcopy(rnd.choice(parent)))   # duplicates (same ids)
            elif r < 0.45 and isinstance(parent[key], (int, float)) and not isinstance(parent[key], bool):
                parent[key] = rnd.choice([parent[key] * 1e9, -parent[key], parent[key] + 1, 0, float("nan")])
            else:
                parent[key] = copy.deepcopy(rnd.choice(WEIRD))
        except (KeyError, IndexError, TypeError):
            pass
    return doc


def dumps(doc):
    # NaN / Infinity as JSON would write them (invalid JSON on purpose sometimes)
    return json.dumps(doc, ensure_ascii=False, allow_nan=True).encode()


def recovered():
    return sum(1 for l in open(LOG, errors="replace") if "Recovered:" in l)


def main():
    shutil.rmtree(ROOT, ignore_errors=True)
    os.makedirs(ROOT)
    srcs = [(os.path.join(SAMPLES, "design"), "Узел.bza"), (os.path.join(SAMPLES, "gear"), "Редуктор.bza")]
    r0 = recovered()
    for i in range(count):
        folder, name = rnd.choice(srcs)
        work = os.path.join(ROOT, f"case{i}")
        shutil.copytree(folder, work)
        # Damage the assembly or one of its parts.
        targets = [f for f in os.listdir(work) if f.endswith((".bza", ".bzc"))]
        victim = os.path.join(work, rnd.choice(targets) if rnd.random() < 0.5 else name)
        files = read(victim)
        r = rnd.random()
        if r < 0.1:
            raw = open(victim, "rb").read()
            open(victim, "wb").write(raw[: rnd.randint(0, len(raw))])   # truncated
        elif r < 0.15:
            files["document.json"] = bytes(rnd.getrandbits(8) for _ in range(200))
            write(victim, files)
        else:
            doc = json.loads(files["document.json"])
            files["document.json"] = dumps(mutate(doc))
            write(victim, files)
        try:
            for d in reversed(call("app.documents")):
                call("app.activate", index=d["index"])
                call("doc.close", discard_changes=True)
        except Exception:
            pass
        status = "?"
        opened = os.path.join(work, name) if victim.endswith(".bza") or rnd.random() < 0.5 else victim
        try:
            call("app.open", path=opened)
            status = "opened"
            time.sleep(0.4)
            if opened.endswith(".bzc"):
                call("part.tree"); call("part.properties")
                call("doc.save")
                print(f"case {i} (part {os.path.basename(victim)}): opened"); continue
            c = call("assembly.components")
            for comp in c["components"][:3]:
                try:
                    call("assembly.edit_in_place", component=comp["id"])
                    time.sleep(0.2)
                    call("part.tree")
                    call("assembly.return_to_assembly")
                except RuntimeError:
                    pass
            for con in c["constraints"][:3]:
                try:
                    call("assembly.edit_constraint", id=con["id"], offset=1)
                except RuntimeError:
                    pass
            call("doc.save")
        except RuntimeError as e:
            status = "refused: " + str(e)[:90]
        except Exception as e:
            print(f"case {i}: DIED? {e}")
            return 2
        print(f"case {i} ({os.path.basename(victim)}): {status}")
    print(f"filefuzz {seed}: {count} cases, recoveries {recovered() - r0}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
