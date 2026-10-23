#!/usr/bin/env python3
"""The safety net (app/Recovery.h) end to end: in an assembly and in a part,
a change then a crash / an exception — in a command, in work put off to the
end of the frame, halfway through drawing the interface. The program must
answer afterwards and the document be exactly as before.

Needs the program started with BZ_FAULT_INJECTION=1 (the debug.fault command).
    python3 recovery.py
"""
import os, shutil, sys, time
from bz import call

WORK = os.environ.get("BZ_CRASHTEST", "/tmp/bz-crashtest")
SAMPLES = os.path.join(WORK, "samples")
ROOT = os.path.join(WORK, "recovery")
shutil.rmtree(ROOT, ignore_errors=True)
shutil.copytree(os.path.join(SAMPLES, "design"), ROOT)
failures = 0


def check(cond, what):
    global failures
    print(("  ok   " if cond else "  FAIL ") + what)
    failures += 0 if cond else 1


def run(state):
    before = state()
    for kind in ("crash", "throw"):
        for where in ("here", "later", "interface"):
            try:
                call("debug.fault", kind=kind, where=where)
            except RuntimeError:
                pass   # "here": the command itself reports the failure
            time.sleep(1.5)   # the recovery escalates when failures come too fast
            check(state() == before, f"{kind} {where}: alive, put back as it was")


for d in reversed(call("app.documents")):
    call("app.activate", index=d["index"])
    call("doc.close", discard_changes=True)
call("app.open", path=os.path.join(ROOT, "Узел.bza"))
time.sleep(1)
run(lambda: ([(c["name"], c["placement"]["origin"]) for c in call("assembly.components")["components"]],
             call("app.documents")[0]["modified"]))
call("app.open", path=os.path.join(ROOT, "Плита.bzc"))
time.sleep(1)
run(lambda: ([(f["name"], f.get("hidden")) for f in call("part.tree")["features"]], call("part.properties")["volume_mm3"]))
print("RESULT:", "PASS" if failures == 0 else f"FAIL ({failures})")
sys.exit(1 if failures else 0)
