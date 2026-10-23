#!/usr/bin/env python3
"""Monkey test: random clicks, double clicks, drags and keys all over the
window of a running BreezeCAD (Xvfb :99), with an assembly open.

    python3 monkey.py SEED ACTIONS

Reports: died / safety-net recoveries (each one a bug) / slow frames.
"""
import os, random, subprocess, sys, time
from bz import call

SP = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get("BZ_CRASHTEST", "/tmp/bz-crashtest")   # samples (samples.py) and copies
SAMPLES = os.path.join(WORK, "samples")
LOG = os.environ.get("BZ_LOG", "/tmp/breezecad-ui.log")
seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
n = int(sys.argv[2]) if len(sys.argv) > 2 else 300
rnd = random.Random(seed)
env = dict(os.environ, DISPLAY=":99")
W, H = 1440, 900
os.makedirs(WORK, exist_ok=True)
journal = open(os.path.join(WORK, f"monkey-{seed}.log"), "w")


def x(*args):
    subprocess.run(["xdotool", *map(str, args)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def running():
    return subprocess.run(["pgrep", "-x", "breezecad"], stdout=subprocess.DEVNULL).returncode == 0


def count(word):
    try:
        return sum(1 for l in open(LOG, errors="replace") if word in l)
    except FileNotFoundError:
        return 0


KEYS = ["Escape", "Return", "Delete", "ctrl+z", "ctrl+y", "ctrl+s", "e", "s", "l", "c", "r", "d", "f", "Home", "Tab",
        "ctrl+a", "BackSpace", "1", "5", "0", "minus", "period", "space", "ctrl+n"]
REGIONS = {  # (x0, y0, x1, y1, weight)
    "ribbon": (0, 80, 1440, 165, 3),
    "tabs": (0, 40, 400, 75, 1),
    "tree": (48, 175, 343, 835, 4),
    "view": (344, 175, 1440, 840, 6),
    "header": (0, 0, 1000, 38, 1),
    "bottom": (0, 842, 600, 875, 1),
}


def point():
    names = list(REGIONS)
    weights = [REGIONS[k][4] for k in names]
    r = REGIONS[rnd.choices(names, weights)[0]]
    return rnd.randint(r[0], r[2]), rnd.randint(r[1], r[3])


def main():
    import shutil
    shutil.rmtree(os.path.join(WORK, "monkey"), ignore_errors=True)
    shutil.copytree(os.path.join(SAMPLES, "design"), os.path.join(WORK, "monkey"))
    try:
        if not call("app.documents"):
            call("app.open", path=os.path.join(WORK, "monkey", "Узел.bza"))
    except Exception:
        pass
    rec0, slow0 = count("Recovered:"), count("Slow frame")
    t0 = time.time()
    for i in range(n):
        k = rnd.random()
        if k < 0.45:
            px, py = point()
            journal.write(f"click {px} {py}\n")
            x("mousemove", px, py)
            time.sleep(0.05)
            x("click", 1 if rnd.random() < 0.85 else 3)
        elif k < 0.55:
            px, py = point()
            journal.write(f"dblclick {px} {py}\n")
            x("mousemove", px, py)
            x("click", "--repeat", 2, "--delay", 60, 1)
        elif k < 0.7:
            px, py = point()
            journal.write(f"drag {px} {py}\n")
            x("mousemove", px, py)
            x("mousedown", 1)
            for _ in range(rnd.randint(3, 12)):
                px += rnd.randint(-40, 40)
                py += rnd.randint(-30, 30)
                x("mousemove", max(0, min(W - 1, px)), max(0, min(H - 1, py)))
                time.sleep(0.02)
            x("mouseup", 1)
        else:
            key = rnd.choice(KEYS)
            journal.write(f"key {key}\n")
            x("key", key)
        journal.flush()
        time.sleep(rnd.choice([0.02, 0.05, 0.15]))
        if not running():
            print(f"monkey {seed}: DIED at action {i} (see monkey-{seed}.log)")
            return 2
        # A modal file dialog (zenity) would block: closed if one appeared.
        subprocess.run(["pkill", "-x", "zenity"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # Back to something to work on now and then.
        if i % 60 == 59:
            try:
                docs = call("app.documents")
                if not docs:
                    call("app.open", path=os.path.join(WORK, "monkey", "Узел.bza"))
            except Exception:
                pass
    print(f"monkey {seed}: {n} actions in {time.time() - t0:.0f} s; recoveries {count('Recovered:') - rec0}; "
          f"slow frames {count('Slow frame') - slow0}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
