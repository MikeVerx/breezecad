#!/usr/bin/env python3
"""Sample documents for the crash tests, made by the program itself:

    design/  Плита.bzc, Узел.bza with «Крышка» made in it (its pin projected
             from the plate's hole: adaptive);
    gear/    Корпус, Шестерня, Колесо and Редуктор.bza (two rotational joints
             and a gear between them).

    python3 samples.py ROOT
"""
import os, shutil, sys, time
from bz import call


def close_all():
    for d in reversed(call("app.documents")):
        call("app.activate", index=d["index"])
        call("doc.close", discard_changes=True)


def part(folder, name, items, h):
    call("app.new_part")
    sk = call("sketch.create", plane="XY")["id"]
    call("sketch.add", sketch=sk, items=items)
    call("feature.extrude", sketch=sk, distance=str(h))
    p = os.path.join(folder, name + ".bzc")
    call("doc.save", path=p)
    call("doc.close")
    return p


def design(root):
    d = os.path.join(root, "design")
    os.makedirs(d)
    plate = part(d, "Плита", [{"type": "rectangle", "corner": [0, 0], "opposite": [80, 50]},
                             {"type": "circle", "center": [20, 25], "radius": 5}], 10)
    call("assembly.new")
    plate_id = call("assembly.place", file=plate)["id"]
    call("doc.save", path=os.path.join(d, "Узел.bza"))
    call("assembly.create_part", plane={"component": plate_id, "point": [60, 40, 10]}, name="Крышка")
    time.sleep(0.4)
    sk = call("sketch.create", plane="XY")["id"]
    call("assembly.project_edge", sketch=sk, component=plate_id, point=[25, 25, 10])
    call("feature.extrude", sketch=sk, distance="8")
    call("assembly.return_to_assembly")
    time.sleep(0.4)
    call("doc.save")
    call("doc.close")


def gear(root):
    g = os.path.join(root, "gear")
    os.makedirs(g)
    base = part(g, "Корпус", [{"type": "rectangle", "corner": [-20, -25], "opposite": [60, 25]},
                             {"type": "circle", "center": [0, 0], "radius": 3}, {"type": "circle", "center": [30, 0], "radius": 3}], 5)
    a = part(g, "Шестерня", [{"type": "circle", "center": [0, 0], "radius": 10}, {"type": "circle", "center": [0, 0], "radius": 3}], 5)
    b = part(g, "Колесо", [{"type": "circle", "center": [0, 0], "radius": 20}, {"type": "circle", "center": [0, 0], "radius": 3}], 5)
    call("assembly.new")
    k = call("assembly.place", file=base)["id"]
    g1 = call("assembly.place", file=a)["id"]
    g2 = call("assembly.place", file=b)["id"]
    call("assembly.move", component=g1, to=[0, 0, 10])
    call("assembly.move", component=g2, to=[30, 0, 10])
    j1 = call("assembly.constrain", type="rotational", first={"component": g1, "point": [3, 0, 10]}, second={"component": k, "point": [3, 0, 5]})
    j2 = call("assembly.constrain", type="rotational", first={"component": g2, "point": [33, 0, 10]}, second={"component": k, "point": [33, 0, 5]})
    call("assembly.constrain", type="gear", drive=j1["id"], driven=j2["id"], ratio=0.5, flipped=True)
    call("doc.save", path=os.path.join(g, "Редуктор.bza"))
    call("doc.close")


if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bz-crashtest/samples"
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    close_all()
    design(root)
    gear(root)
    print("samples in", root)
