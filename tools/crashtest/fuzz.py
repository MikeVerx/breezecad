#!/usr/bin/env python3
"""Crash test: random chains of assembly / in-place / part commands.

    python3 fuzz.py SEED STEPS

After every step: the program must answer; "Recovered:" lines in the log
are failures the safety net caught (each one a bug to fix). Writes the
steps to fuzz-<seed>.log so a failure can be replayed.
"""
import json, os, random, shutil, sys, time, traceback
from bz import call

SP = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get("BZ_CRASHTEST", "/tmp/bz-crashtest")   # samples (samples.py) and copies
SAMPLES = os.path.join(WORK, "samples")
LOG = os.environ.get("BZ_LOG", "/tmp/breezecad-ui.log")
seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
steps = int(sys.argv[2]) if len(sys.argv) > 2 else 200
rnd = random.Random(seed)
ROOT = os.path.join(WORK, f"fuzz-{seed}")
shutil.rmtree(ROOT, ignore_errors=True)
shutil.copytree(os.path.join(SAMPLES, "design"), os.path.join(ROOT, "d"))
shutil.copytree(os.path.join(SAMPLES, "gear"), os.path.join(ROOT, "g"))
PARTS = [os.path.join(ROOT, "d", "Плита.bzc"), os.path.join(ROOT, "g", "Колесо.bzc"),
         os.path.join(ROOT, "g", "Шестерня.bzc"), os.path.join(ROOT, "g", "Корпус.bzc")]
STATS = {}
journal = open(os.path.join(WORK, f"fuzz-{seed}.log"), "w")


def recovered_count():
    try:
        return sum(1 for l in open(LOG, errors="replace") if "Recovered:" in l)
    except FileNotFoundError:
        return 0


def alive():
    for _ in range(3):
        try:
            call("app.documents")
            return True
        except Exception as e:
            if "Connection refused" in str(e) or "timed out" in str(e):
                return False
            time.sleep(0.3)
    return True


def safe(cmd, **args):
    journal.write(json.dumps([cmd, args], ensure_ascii=False) + "\n")
    journal.flush()
    try:
        r = call(cmd, **args)
        STATS.setdefault(cmd, [0, 0, {}])[0] += 1
        return r
    except RuntimeError as e:
        st = STATS.setdefault(cmd, [0, 0, {}])
        st[1] += 1
        msg = str(e).split(": ", 1)[-1][:70]
        st[2][msg] = st[2].get(msg, 0) + 1
        return {"error": str(e)[:200]}


def comps():
    r = safe("assembly.components")
    return r if isinstance(r, dict) and "components" in r else {"components": [], "constraints": []}


def rpoint():
    return [rnd.uniform(-30, 90), rnd.uniform(-30, 60), rnd.uniform(-10, 30)]


def geom(c):
    ids = [x["id"] for x in c["components"]]
    choice = rnd.random()
    if choice < 0.15 or not ids:
        return {"origin": rnd.choice(["xy", "xz", "yz", "x", "y", "z", "centre"])}
    cid = rnd.choice(ids)
    if choice < 0.6:
        f = safe("assembly.faces", component=cid, limit=12)
        if isinstance(f, dict) and f.get("faces"):
            return {"component": cid, "face": rnd.choice(f["faces"])["face"]}
    if choice < 0.75:
        return {"component": cid, "element": rnd.choice(["xy", "xz", "yz", "x", "y", "z", "centre"])}
    return {"component": cid, "point": rpoint()}


TYPES = ["mate", "insert", "angle", "tangent", "axis_mate", "rotational", "slider", "cylindrical", "planar", "ball",
         "rigid", "symmetry", "gear", "screw"]


def step_assembly(c):
    ids = [x["id"] for x in c["components"]]
    cons = [x["id"] for x in c["constraints"]]
    r = rnd.random()
    if r < 0.12 or len(ids) < 2:
        return safe("assembly.place", file=rnd.choice(PARTS))
    if r < 0.35:
        t = rnd.choice(TYPES)
        args = {"type": t}
        if t in ("gear", "screw"):
            joints = [x["id"] for x in c["constraints"] if x["type"] in ("rotational", "slider")] or cons or [1]
            args.update(drive=rnd.choice(joints), driven=rnd.choice(joints), ratio=rnd.choice([0.5, 2, -1, 0, 1e6]))
        else:
            args.update(first=geom(c), second=geom(c))
            if t == "symmetry":
                args["plane"] = geom(c)
            if rnd.random() < 0.3:
                args["offset"] = rnd.choice([0, 5, -5, 1e9, -1e-9])
            if rnd.random() < 0.2 and t in ("mate", "angle"):
                args["range"] = sorted([rnd.uniform(-50, 50), rnd.uniform(-50, 50)])
            if rnd.random() < 0.3:
                args["flipped"] = rnd.random() < 0.5
        return safe("assembly.constrain", **args)
    if r < 0.45 and cons:
        args = {"id": rnd.choice(cons)}
        k = rnd.random()
        if k < 0.3:
            args["offset"] = rnd.choice([0, 3, -7, 1e7])
        elif k < 0.5:
            args["angle"] = rnd.choice([0, 45, 90, 180, 720, -30])
        elif k < 0.7:
            args.update(locked=True, position=rnd.choice([0, 30, 400, -1000, 5]))
        elif k < 0.85:
            args["suppressed"] = rnd.random() < 0.5
        else:
            args["flipped"] = rnd.random() < 0.5
        return safe("assembly.edit_constraint", **args)
    if r < 0.53:
        return safe("assembly.move", component=rnd.choice(ids), by=[rnd.uniform(-20, 20) for _ in range(3)])
    if r < 0.57:
        return safe("assembly.move", component=rnd.choice(ids),
                    rotate={"axis": [rnd.random(), rnd.random(), rnd.random() + 0.1], "angle": rnd.uniform(-180, 180)})
    if r < 0.62:
        return safe("assembly.remove", **({"component": rnd.choice(ids)} if rnd.random() < 0.5 or not cons else {"constraint": rnd.choice(cons)}))
    if r < 0.66:
        return safe("assembly.ground", component=rnd.choice(ids), grounded=rnd.random() < 0.5)
    if r < 0.74:
        return safe("doc.undo") if rnd.random() < 0.6 else safe("doc.redo")
    if r < 0.82:
        return safe("assembly.edit_in_place", component=rnd.choice(ids))
    if r < 0.88:
        return safe("assembly.create_part", name=rnd.choice(["А", "Б/В", "  ", "x" * 200, "Деталь"]),
                    plane=geom(c))
    if r < 0.91:
        return safe("assembly.clashes")
    if r < 0.95:
        return safe("doc.save")
    return safe("view.set", fit=True)


def step_in_place():
    r = rnd.random()
    tree = safe("part.tree")
    feats = tree.get("features", []) if isinstance(tree, dict) else []
    sketches = [f["id"] for f in feats if f.get("type") == "sketch"]
    if r < 0.2:
        return safe("sketch.create", plane=rnd.choice(["XY", "XZ", "YZ"]))
    if r < 0.35 and sketches:
        return safe("sketch.add", sketch=rnd.choice(sketches),
                    items=[{"type": "circle", "center": [rnd.uniform(-20, 60), rnd.uniform(-20, 40)], "radius": rnd.uniform(0.1, 15)}])
    if r < 0.45 and sketches:
        return safe("feature.extrude", sketch=rnd.choice(sketches), distance=str(rnd.choice([1, 5, 20, -3, 0])))
    if r < 0.6 and sketches:
        c = comps()
        ids = [x["id"] for x in c["components"]]
        if ids:
            return safe("assembly.project_edge", sketch=rnd.choice(sketches), component=rnd.choice(ids), point=rpoint(),
                        tolerance=rnd.choice([1, 50, 1000]))
    if r < 0.68:
        params = safe("part.parameters")
        ps = params.get("parameters", []) if isinstance(params, dict) else []
        if ps:
            p = rnd.choice(ps)
            return safe("part.set_parameter", name=p["name"], value=str(rnd.choice([1, 5, 30, 0, -5, 1000])))
    if r < 0.74 and feats:
        return safe("part.delete", id=rnd.choice(feats)["id"])
    if r < 0.8 and feats:
        return safe("part.suppress", id=rnd.choice(feats)["id"], suppressed=rnd.random() < 0.5)
    if r < 0.88:
        return safe("doc.undo") if rnd.random() < 0.6 else safe("doc.redo")
    if r < 0.92:
        return safe("doc.save")
    return safe("assembly.return_to_assembly")


def main():
    for d in reversed(safe("app.documents") or []):
        safe("app.activate", index=d["index"])
        safe("doc.close", discard_changes=True)
    safe("app.open", path=os.path.join(ROOT, "d", "Узел.bza"))
    start = recovered_count()
    t0 = time.time()
    slow = []
    for i in range(steps):
        docs = safe("app.documents")
        in_place = False
        if isinstance(docs, list) and docs:
            tree = safe("part.tree")
            in_place = isinstance(tree, dict) and "features" in tree and any(d.get("type") == "assembly" and d.get("active") for d in docs)
        if not docs or isinstance(docs, dict):
            safe("app.open", path=os.path.join(ROOT, "d", "Узел.bza"))
            continue
        a = time.time()
        if rnd.random() < 0.02:
            safe("doc.close", discard_changes=True)
            safe("app.open", path=os.path.join(ROOT, rnd.choice(["d/Узел.bza", "g/Редуктор.bza"])))
        elif in_place:
            step_in_place()
        else:
            step_assembly(comps())
        took = time.time() - a
        if took > 2.0:
            slow.append((i, round(took, 2)))
        if not alive():
            print(f"seed {seed}: DIED at step {i}")
            return 2
        n = recovered_count() - start
        if n:
            print(f"seed {seed}: safety net caught {n} failure(s) by step {i}")
            start += n
    for k, (ok, bad, why) in sorted(STATS.items()):
        top = sorted(why.items(), key=lambda x: -x[1])[:2]
        print(f"  {k}: ok {ok}, refused {bad} {top}")
    print(f"seed {seed}: {steps} steps OK in {time.time() - t0:.0f} s; slow steps: {slow[:10]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
