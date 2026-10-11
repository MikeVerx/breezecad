#!/usr/bin/env python3
"""Runs BreezeCAD under watch and writes a short report on what was slow,
what hung and what crashed, each tied to the action that caused it.

    python tools/monitor/run.py [--bin PATH] [-- program arguments]
    python tools/monitor/run.py --scenario bracket          (Linux, Xvfb)
    python tools/monitor/run.py --tester BreezeCAD-отчёт-….zip [--bin <папка сборки>]
                                       (a report sent by a beta tester; with --bin
                                       and `pip install pefile`, Windows stack
                                       frames get the names of DLL functions)

Use the program as usual and close it: the report is written to
tools/monitor/reports/<date-time>.md (+ .json; raw data in <date-time>/).
Works on Windows and Linux; Python 3.8+, standard library only.

The program writes its events (actions, frames, rebuilds, hangs, memory,
warnings, crashes) to the file named by BZ_MONITOR; see src/core/Monitor.h.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import zipfile
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
WINDOWS = os.name == "nt"
EXE = "breezecad.exe" if WINDOWS else "breezecad"


# ---------------------------------------------------------------- running

def find_binary(given: str | None) -> Path:
    if given:
        p = Path(given)
        if p.is_dir():
            p = p / EXE
        if not p.exists():
            sys.exit(f"Нет программы: {p}")
        return p.resolve()
    if os.environ.get("BZ_BIN"):
        return find_binary(os.environ["BZ_BIN"])
    found = [p for p in (ROOT / "build").glob(f"*/bin/{EXE}") if p.is_file()]
    for p in (Path.cwd() / EXE, HERE / EXE):
        if p.is_file():
            found.append(p)
    if not found:
        sys.exit(f"Не нашёл {EXE}: укажите --bin путь\\к\\{EXE}")
    # A release build measures what users get; otherwise the newest one.
    found.sort(key=lambda p: ("release" in str(p).lower(), p.stat().st_mtime), reverse=True)
    return found[0].resolve()


class StderrReader(threading.Thread):
    """Keeps every stderr/stdout line with its time since the start."""

    def __init__(self, stream, start: float, raw: Path):
        super().__init__(daemon=True)
        self.stream, self.t0, self.lines = stream, start, []
        self.raw = open(raw, "w", encoding="utf-8", errors="replace")

    def run(self):
        for b in iter(self.stream.readline, b""):
            line = b.decode("utf-8", errors="replace").rstrip("\r\n")
            self.lines.append((time.monotonic() - self.t0, line))
            self.raw.write(line + "\n")
            self.raw.flush()
        self.raw.close()


def stack_of_hang(pid: int, out: Path) -> tuple[str, float]:
    """All threads' stacks of a hung process (Linux with gdb), and how long
    gdb held it stopped (taken off the hang's length)."""
    if WINDOWS or not shutil.which("gdb"):
        return "", 0.0
    began = time.monotonic()
    try:
        r = subprocess.run(["gdb", "-p", str(pid), "-batch", "-nx", "-ex", "set pagination off",
                            "-ex", "thread apply all bt 30"],
                           capture_output=True, text=True, timeout=60, errors="replace")
        out.write_text(r.stdout + r.stderr, encoding="utf-8")
        return r.stdout, time.monotonic() - began
    except (subprocess.SubprocessError, OSError):
        return "", time.monotonic() - began


def main_thread_frames(gdb_text: str, count: int = 12) -> list[str]:
    """The window's thread (Thread 1) from `thread apply all bt`, short."""
    frames, inside = [], False
    for line in gdb_text.splitlines():
        if line.startswith("Thread "):
            inside = bool(re.match(r"Thread 1 \(", line))
            continue
        if inside and line.startswith("#"):
            m = re.match(r"#\d+\s+(?:0x[0-9a-f]+ in )?(.+?)(?: \(.*\))?(?: at (\S+))?$", line)
            if m:
                where = f" ({Path(m.group(2)).name})" if m.group(2) else ""
                frames.append(m.group(1)[:110] + where)
    # Skip the waiting-in-the-kernel top when it is just a lock or a poll.
    return frames[:count]


def start_xvfb(display: str):
    if subprocess.run(["pgrep", "-f", f"Xvfb {display} "], capture_output=True).returncode != 0:
        subprocess.Popen(["Xvfb", display, "-screen", "0", "1600x1000x24"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.7)


def run(args) -> Path:
    binary = find_binary(args.bin)
    stamp = dt.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    reports = HERE / "reports"
    raw = reports / stamp
    raw.mkdir(parents=True, exist_ok=True)
    events_file = raw / "events.jsonl"

    env = dict(os.environ)
    env["BZ_MONITOR"] = str(events_file)
    env["BZ_MONITOR_HANG_MS"] = str(args.hang_ms)
    program_args = list(args.program_args)
    scenario = None
    if args.scenario:
        if WINDOWS:
            sys.exit("Сценарии — только на Linux (Xvfb + xdotool).")
        scenario = resolve_scenario(args.scenario)
        program_args = scenario["args"] + program_args
        env["DISPLAY"] = args.display
        env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
        env["LANG"] = "C.UTF-8"
        env["BZ_SHOT_DIR"] = str(raw / "shots")
        (raw / "shots").mkdir(exist_ok=True)
        start_xvfb(args.display)

    print(f"Программа: {binary}")
    print(f"Отчёт будет: {reports / (stamp + '.md')}")
    if not scenario:
        print("Работайте как обычно и закройте программу (Ctrl+C здесь — остановить).")

    cmd = [str(binary)] + program_args
    start = time.monotonic()
    proc = subprocess.Popen(cmd, cwd=str(binary.parent), env=env, stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    reader = StderrReader(proc.stdout, start, raw / "output.log")
    reader.start()

    hang_stacks: dict[float, tuple[list[str], float]] = {}
    stopped_by = None
    scenario_proc = None
    if scenario:
        senv = dict(env, UI_ATTACHED="1", UI_DISPLAY=args.display, SHOTS=str(raw / "shots"),
                    BZ_BIN=str(binary.parent))
        scenario_proc = subprocess.Popen(["bash", str(scenario["script"])], env=senv, cwd=str(ROOT),
                                         stdout=open(raw / "scenario.log", "w"), stderr=subprocess.STDOUT)
    try:
        tail, pending = None, ""
        while proc.poll() is None:
            time.sleep(0.2)
            # Live: a hang gets its stack taken while it lasts.
            if tail is None and events_file.exists():
                tail = open(events_file, encoding="utf-8", errors="replace")
            if tail:
                pending += tail.read()
                lines = pending.split("\n")
                pending = lines.pop()  # an unfinished line waits for the rest
                for line in lines:
                    if '"k":"hang"' in line and len(hang_stacks) < 5:
                        try:
                            ev = json.loads(line)
                        except ValueError:
                            continue
                        print(f"  зависание с {ev['since']:.1f} с — снимаю стек…")
                        text, held = stack_of_hang(proc.pid, raw / f"hang-{len(hang_stacks) + 1}.txt")
                        hang_stacks[ev["since"]] = (main_thread_frames(text), held)
            if scenario_proc and scenario_proc.poll() is not None:
                time.sleep(args.settle)
                stopped_by = f"сценарий закончился (код {scenario_proc.returncode}), программа остановлена монитором"
                proc.terminate()
                break
    except KeyboardInterrupt:
        stopped_by = "остановлено вручную (Ctrl+C)"
        proc.terminate()
    try:
        code = proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
        code = proc.wait()
        stopped_by = (stopped_by or "") + "; не завершилась сама — убита"
    reader.join(timeout=5)
    if tail:
        tail.close()
    duration = time.monotonic() - start

    meta = {"binary": str(binary), "args": program_args, "started": stamp, "duration_s": round(duration, 1),
            "exit_code": code, "stopped_by": stopped_by, "scenario": scenario["name"] if scenario else None,
            "software_gl": env.get("LIBGL_ALWAYS_SOFTWARE") == "1", "hang_ms": args.hang_ms}
    report = analyze(meta, events_file, reader.lines, hang_stacks, binary, raw)
    (reports / (stamp + ".json")).write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    md = reports / (stamp + ".md")
    md.write_text(render(report), encoding="utf-8")
    print(f"\nОтчёт: {md}")
    print(report["verdict"])
    return md


SCENARIOS = {
    # name: (script, program arguments relative to the repo)
    "bracket": ("tools/monitor/scenarios/bracket.sh", ["tests/data/кронштейн.bzc"]),
    "assembly": ("tools/monitor/scenarios/assembly.sh", ["tests/data/кронштейны.bza"]),
}


def resolve_scenario(name: str) -> dict:
    if name in SCENARIOS:
        script, files = SCENARIOS[name]
        return {"name": name, "script": ROOT / script, "args": [str(ROOT / f) for f in files]}
    p = Path(name)
    if not p.exists():
        sys.exit(f"Нет сценария {name}; есть: {', '.join(SCENARIOS)} или путь к .sh")
    return {"name": p.stem, "script": p.resolve(), "args": []}


# ---------------------------------------------------------------- analysis

def pct(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    v = sorted(values)
    k = (len(v) - 1) * q
    lo, hi = int(k), min(int(k) + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def norm_message(msg: str) -> str:
    """Groups messages that differ only in numbers, paths or names in quotes."""
    msg = re.sub(r"«[^»]*»", "«…»", msg)
    msg = re.sub(r"(/|[A-Za-z]:\\)[^\s:]+", "<путь>", msg)
    return re.sub(r"\d+(\.\d+)?", "#", msg)


def symbolize(stderr_lines: list[str], binary: Path) -> list[str]:
    """Linux: frames of backtrace_symbols_fd → function (file:line)."""
    out = []
    for line in stderr_lines:
        m = re.match(r"(\S+)\(([^)+]*)\+(0x[0-9a-f]+)\)\s*\[(0x[0-9a-f]+)\]", line)
        if not m:
            continue
        module, sym, off, _addr = m.groups()
        text = None
        if not sym and shutil.which("addr2line"):
            mod = module if os.path.isabs(module) else str(binary.parent / module)
            try:
                r = subprocess.run(["addr2line", "-e", mod, "-f", "-C", "-p", off], capture_output=True,
                                   text=True, timeout=10)
                text = r.stdout.strip().replace(str(ROOT) + "/", "")
            except (subprocess.SubprocessError, OSError):
                pass
        if not text or text.startswith("??"):
            text = demangle(sym) if sym else f"{Path(module).name}+{off}"
        out.append(text)
    return out


def demangle(sym: str) -> str:
    if shutil.which("c++filt"):
        try:
            return subprocess.run(["c++filt", sym], capture_output=True, text=True, timeout=5).stdout.strip()
        except (subprocess.SubprocessError, OSError):
            pass
    return sym


def analyze(meta, events_file: Path, out_lines, hang_stacks, binary: Path, raw: Path) -> dict:
    events = []
    if events_file.exists():
        for line in events_file.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                events.append(json.loads(line))
            except ValueError:
                pass  # the last line of a crashed process may be cut

    # The action in force at each moment.
    acts = [(e["t"], f'{e["src"]}: {e["name"]}' if e["name"] else e["src"]) for e in events if e["k"] == "act"]

    def action_at(t: float) -> str:
        name = "(до первого действия: запуск)"
        for at, a in acts:
            if at > t:
                break
            name = a
        return name

    frames = [e for e in events if e["k"] == "frame"]
    frame_ms = [f["ms"] for f in frames]
    first_frame = frames[0]["t"] if frames else None

    # --- crashes
    crashes = []
    code = meta["exit_code"]
    crash_events = [e for e in events if e["k"] == "crash"]
    clean_exit = any(e["k"] == "exit" for e in events)
    signal_name = None
    if code is not None and code < 0 and not meta["stopped_by"]:
        try:
            signal_name = signal.Signals(-code).name
        except ValueError:
            signal_name = f"сигнал {-code}"
    win_exception = code is not None and code >= 0xC0000000 or (code is not None and code < 0 and WINDOWS)
    if crash_events or signal_name or win_exception or (code not in (0, None) and not clean_exit and not meta["stopped_by"]):
        stack_lines = []
        txt = [l for _, l in out_lines]
        if "Stack:" in txt:
            stack_lines = symbolize(txt[txt.index("Stack:") + 1:], binary)
            # The handler's own frames are noise.
            while stack_lines and re.search(r"OnSignal|OnTerminate|__restore_rt|sigaction|killpg|raise|pthread_kill|gsignal|"
                                            r"abort|rethrow|unexpected|__cxa_|libc\.so|terminate", stack_lines[0]):
                stack_lines.pop(0)
        win_stack = [l for _, l in out_lines if re.search(r"ERROR\s+#\d+ ", l)]
        exc = [l for _, l in out_lines if "Crash:" in l]
        last_log = [l for _, l in out_lines][-25:]
        ce = crash_events[0] if crash_events else {}
        uncaught = next((re.sub(r"^.*Crash: uncaught exception: ", "", l).split(" (last action")[0] for l in exc
                         if "uncaught exception:" in l), None)
        dump = raw / "breezecad.dmp"
        crashes.append({
            "t": ce.get("t"),
            "what": (f"исключение «{uncaught}»" if uncaught else None) or ce.get("what") or signal_name or (f"код выхода {code} (0x{code & 0xFFFFFFFF:08X})" if code else "?"),
            "action": ce.get("act") or (action_at(ce["t"]) if ce else (acts[-1][1] if acts else "")),
            "exit_code": code,
            "messages": exc[:5],
            "stack": (stack_lines or [re.sub(r"^.*ERROR\s+", "", l) for l in win_stack])[:25],
            "minidump": str(dump) if dump.exists() else None,
            "log_tail": last_log,
        })

    # --- hangs
    hangs = []
    open_hang = None
    for e in events:
        if e["k"] == "hang":
            open_hang = e
        elif e["k"] == "hang_end" and open_hang:
            stack, held = hang_stacks.get(open_hang["since"], ([], 0.0))
            hangs.append({"since": open_hang["since"], "ms": e["ms"],
                          "action": open_hang.get("act") or action_at(open_hang["since"]), "stack": stack,
                          "gdb_pause_ms": round(held * 1000)})
            open_hang = None
    if open_hang:  # still hung when it ended
        end = events[-1]["t"] if events else open_hang["t"]
        stack, held = hang_stacks.get(open_hang["since"], ([], 0.0))
        hangs.append({"since": open_hang["since"], "ms": (end - open_hang["since"]) * 1000, "action": open_hang.get("act"),
                      "stack": stack, "unfinished": True})
    # Frames longer than the threshold are hangs too, even if the watchdog
    # (polling) missed them; merge by time.
    for f in frames:
        if f["ms"] >= meta["hang_ms"] and not any(abs((f["t"] - f["ms"] / 1000) - h["since"]) < 0.5 for h in hangs):
            hangs.append({"since": f["t"] - f["ms"] / 1000, "ms": f["ms"], "action": action_at(f["t"] - f["ms"] / 1000),
                          "stack": [], "parts": f.get("parts", {})})
    for h in hangs:  # the frame that contained it says where the time went
        for f in frames:
            if f["t"] >= h["since"] and f["ms"] >= meta["hang_ms"]:
                h.setdefault("parts", f.get("parts", {}))
                break
    hangs.sort(key=lambda h: -h["ms"])

    # --- frames by action
    by_action = defaultdict(list)
    for f in frames:
        by_action[action_at(f["t"] - f["ms"] / 1000)].append(f)
    groups = []
    for action, fs in by_action.items():
        ms = [f["ms"] for f in fs]
        stages = defaultdict(float)
        slow = [f for f in fs if f["ms"] > 33]
        for f in slow:
            for k, v in f.get("parts", {}).items():
                stages[k] += v
        rest = sum(f["ms"] for f in slow) - sum(stages.values())
        if rest > 0:
            stages["прочее"] = rest   # what no prof::Scope covers
        top = sorted(stages.items(), key=lambda kv: -kv[1])[:4]
        groups.append({"action": action, "frames": len(fs), "median": round(pct(ms, .5), 1), "p95": round(pct(ms, .95), 1),
                       "worst": round(max(ms), 1), "slow": len(slow), "slow_ms_total": round(sum(f["ms"] for f in slow)),
                       "stages": [[k, round(v / max(1, len(slow)), 1)] for k, v in top]})
    groups.sort(key=lambda g: (-g["slow_ms_total"], -g["worst"]))
    worst = sorted(frames, key=lambda f: -f["ms"])[:10]
    worst_frames = [{"t": round(f["t"], 2), "ms": round(f["ms"], 1), "action": action_at(f["t"] - f["ms"] / 1000),
                     "parts": f.get("parts", {})} for f in worst if f["ms"] > 33]

    # --- long operations
    timed = [{"t": round(e["t"], 2), "what": e["what"], "detail": e["detail"], "ms": round(e["ms"], 1),
              "action": action_at(e["t"])} for e in events if e["k"] == "timed"]
    timed.sort(key=lambda x: -x["ms"])

    # --- errors and warnings (log events + stderr lines the log did not see)
    msgs = defaultdict(lambda: {"count": 0, "first": None, "actions": set(), "level": "", "example": ""})
    for e in events:
        if e["k"] == "log":
            g = msgs[(e["lvl"], norm_message(e["msg"]))]
            g["count"] += 1
            g["level"] = e["lvl"]
            g["example"] = g["example"] or e["msg"]
            g["first"] = g["first"] if g["first"] is not None else e["t"]
            g["actions"].add(action_at(e["t"]))
    for t, line in out_lines:
        if re.match(r"\d\d:\d\d:\d\d\.\d+ (INFO|DEBUG|WARN|ERROR)", line) or not line.strip():
            continue  # the log's own lines are already counted
        if re.search(r"error|warning|fail|exception|assert|fatal|ошибк", line, re.I) and "Stack:" not in line:
            g = msgs[("stderr", norm_message(line))]
            g["count"] += 1
            g["level"] = "stderr"
            g["example"] = g["example"] or line
            g["first"] = g["first"] if g["first"] is not None else t
            g["actions"].add(action_at(t))
    messages = [{"level": v["level"], "count": v["count"], "first": round(v["first"] or 0, 1), "text": v["example"][:200],
                 "actions": sorted(v["actions"])[:3]} for v in msgs.values()]
    messages.sort(key=lambda m: ({"error": 0, "stderr": 1, "warning": 2}.get(m["level"], 3), -m["count"]))

    # --- memory
    mem = [(e["t"], e["mb"]) for e in events if e["k"] == "mem"]
    memory = None
    if mem:
        jumps = defaultdict(float)
        for (t0, a), (t1, b) in zip(mem, mem[1:]):
            if b > a:
                jumps[action_at(t1)] += b - a
        memory = {"start_mb": mem[0][1], "peak_mb": max(m for _, m in mem), "end_mb": mem[-1][1],
                  "growth_mb": round(mem[-1][1] - mem[0][1], 1),
                  "growth_by_action": [[a, round(v, 1)] for a, v in sorted(jumps.items(), key=lambda kv: -kv[1])[:5] if v >= 5]}

    stats = None
    if frame_ms:
        stats = {"frames": len(frame_ms), "mean": round(sum(frame_ms) / len(frame_ms), 1), "p50": round(pct(frame_ms, .5), 1),
                 "p95": round(pct(frame_ms, .95), 1), "p99": round(pct(frame_ms, .99), 1), "worst": round(max(frame_ms), 1),
                 "over_33": sum(m > 33 for m in frame_ms), "over_100": sum(m > 100 for m in frame_ms)}

    if crashes:
        verdict = f"ВЫЛЕТ: {crashes[0]['what']} после «{crashes[0]['action']}»"
    elif hangs:
        verdict = f"Зависаний: {len(hangs)}, худшее {hangs[0]['ms'] / 1000:.1f} с после «{hangs[0]['action']}»"
    elif stats:
        verdict = f"Без вылетов и зависаний; кадр p95 {stats['p95']} мс, худший {stats['worst']} мс"
    else:
        verdict = "Нет данных о кадрах: программа не дошла до окна?"
    return {"meta": meta, "verdict": verdict, "startup_s": round(first_frame, 2) if first_frame is not None else None,
            "crashes": crashes, "hangs": hangs, "frame_stats": stats, "frames_by_action": groups,
            "worst_frames": worst_frames, "long_operations": timed[:15], "messages": messages, "memory": memory,
            "actions": len(acts)}


# ---------------------------------------------------------------- report

def stages_text(parts: dict | list) -> str:
    items = parts.items() if isinstance(parts, dict) else parts
    items = sorted(items, key=lambda kv: -kv[1])[:4]
    return ", ".join(f"{k} {v:.0f}" for k, v in items) or "—"


def cell(s) -> str:
    return str(s).replace("|", "\\|").replace("\n", " ")


def render(r: dict) -> str:
    m = r["meta"]
    L = [f"# Мониторинг BreezeCAD — {m['started'].replace('_', ' ')}", "",
         f"**Итог:** {r['verdict']}", "",
         f"Программа `{m['binary']}`" + (f", аргументы `{' '.join(m['args'])}`" if m["args"] else "") + "  ",
         f"Сессия {m['duration_s']} с, действий {r['actions']}, код выхода {m['exit_code']}"
         + (f" ({m['stopped_by']})" if m["stopped_by"] else "") + "  ",
         f"Старт до первого кадра: {r['startup_s']} с" if r["startup_s"] is not None else "Кадров не было",
         ]
    if m["scenario"]:
        L.append(f"Сценарий: {m['scenario']}" + (" — программный OpenGL (Mesa llvmpipe): время кадра выше, чем на видеокарте"
                                                  if m["software_gl"] else ""))
    L.append("")

    L.append("## 1. Вылеты")
    if not r["crashes"]:
        L.append("Нет.")
    for c in r["crashes"]:
        L.append(f"- **{c['what']}**" + (f" на {c['t']:.1f} с" if c["t"] else "") + f", действие: «{c['action']}», код {c['exit_code']}")
        for msg in c["messages"]:
            L.append(f"  - `{cell(msg)}`")
        if c["minidump"]:
            L.append(f"  - минидамп: `{c['minidump']}`")
        if c["stack"]:
            L.append("  - стек:")
            L += [f"    {i}. `{cell(s)}`" for i, s in enumerate(c["stack"][:15])]
        if c["log_tail"]:
            L.append("  <details><summary>последние строки лога</summary>\n\n```")
            L += c["log_tail"]
            L.append("```\n  </details>")
    L.append("")

    L.append(f"## 2. Зависания (окно не отвечало ≥ {m['hang_ms']} мс)")
    if not r["hangs"]:
        L.append("Нет.")
    for h in r["hangs"]:
        pause = f" (в т.ч. до {h['gdb_pause_ms'] / 1000:.1f} с — пауза на снятие стека)" if h.get("gdb_pause_ms") else ""
        L.append(f"- **{h['ms'] / 1000:.1f} с**{pause} на {h['since']:.1f} с, действие: «{h['action']}»"
                 + (" — не закончилось до конца сессии" if h.get("unfinished") else "")
                 + (f"; стадии кадра, мс: {stages_text(h['parts'])}" if h.get("parts") else ""))
        if h["stack"]:
            L.append("  - главный поток во время зависания:")
            L += [f"    - `{cell(s)}`" for s in h["stack"][:10]]
    L.append("")

    L.append("## 3. Просадки кадров")
    s = r["frame_stats"]
    if s:
        L.append(f"Кадров {s['frames']}: средний {s['mean']} мс, p50 {s['p50']}, p95 {s['p95']}, p99 {s['p99']}, худший {s['worst']}; "
                 f"> 33 мс — {s['over_33']}, > 100 мс — {s['over_100']}.")
        L.append("")
        L.append("По действиям (кадры после действия до следующего; стадии — среднее в медленных кадрах, мс):")
        L.append("")
        L.append("| Действие | Кадров | Медиана | p95 | Худший | > 33 мс | Где время |")
        L.append("|---|---:|---:|---:|---:|---:|---|")
        for g in r["frames_by_action"][:15]:
            L.append(f"| {cell(g['action'])} | {g['frames']} | {g['median']} | {g['p95']} | {g['worst']} | {g['slow']} | "
                     f"{cell(stages_text(g['stages']))} |")
        if r["worst_frames"]:
            L.append("")
            L.append("Худшие кадры:")
            L += [f"- {w['ms']} мс на {w['t']} с — «{w['action']}»: {stages_text(w['parts'])}" for w in r["worst_frames"]]
    else:
        L.append("Нет данных.")
    L.append("")

    L.append("## 4. Долгие операции")
    if r["long_operations"]:
        L.append("| Что | мс | Подробно | Действие |")
        L.append("|---|---:|---|---|")
        L += [f"| {cell(o['what'])} | {o['ms']} | {cell(o['detail'])} | {cell(o['action'])} |" for o in r["long_operations"]]
    else:
        L.append("Нет.")
    L.append("")

    L.append("## 5. Ошибки и предупреждения")
    if r["messages"]:
        L.append("| Уровень | Раз | Первое, с | Текст | Действие |")
        L.append("|---|---:|---:|---|---|")
        L += [f"| {x['level']} | {x['count']} | {x['first']} | {cell(x['text'])} | {cell('; '.join(x['actions']))} |"
              for x in r["messages"][:25]]
    else:
        L.append("Нет.")
    L.append("")

    L.append("## 6. Память")
    mm = r["memory"]
    if mm:
        L.append(f"В начале {mm['start_mb']} МБ, пик {mm['peak_mb']} МБ, в конце {mm['end_mb']} МБ (рост {mm['growth_mb']} МБ).")
        if mm["growth_by_action"]:
            L.append("Рост по действиям (замер раз в секунду — может лечь на следующее действие): " + "; ".join(f"«{a}» +{v} МБ" for a, v in mm["growth_by_action"]))
    else:
        L.append("Нет данных.")
    L.append("")
    L.append("Сырые данные: папка рядом с отчётом (events.jsonl, output.log, hang-*.txt).")
    return "\n".join(L) + "\n"


# ---------------------------------------------------------------- main

def name_windows_frames(lines: list[str], folder: Path) -> list[str]:
    """"libTKBO.dll+0x169c34" → "… = BOPTools_AlgoTools::ComputeState(…) + 0xe4" (nearest exported
    function of that DLL; optional: needs pefile and the same build's DLLs)."""
    try:
        import bisect
        import pefile  # type: ignore
    except ImportError:
        return lines
    cache: dict[str, tuple[list[int], list[str]] | None] = {}

    def table(module: str):
        if module not in cache:
            cache[module] = None
            f = folder / module
            if f.exists():
                pe = pefile.PE(str(f), fast_load=True)
                pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
                syms = sorted((e.address, e.name.decode()) for e in getattr(pe, "DIRECTORY_ENTRY_EXPORT", None).symbols
                              if e.name) if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") else []
                cache[module] = ([a for a, _ in syms], [n for _, n in syms])
        return cache[module]

    out = []
    for line in lines:
        m = re.search(r"([\w.+-]+\.dll)\+0x([0-9a-f]+)", line, re.I)
        t = table(m.group(1)) if m else None
        if m and t and t[0]:
            addr = int(m.group(2), 16)
            i = bisect.bisect_right(t[0], addr) - 1
            if i >= 0:
                line += f"  = {demangle(t[1][i])} + 0x{addr - t[0][i]:x}"
        out.append(line)
    return out


def tester_report(path: Path, build: Path | None) -> Path:
    """A zip (or its unpacked folder) from the testers' build → the usual report, with the tester's
    words and the computer on top."""
    if path.suffix.lower() == ".zip":
        out = HERE / "reports" / "testers" / path.stem
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir(parents=True)
        with zipfile.ZipFile(path) as z:
            z.extractall(out)
        dirs = [d for d in out.iterdir() if d.is_dir()]
        raw = dirs[0] if len(dirs) == 1 else out
    else:
        raw = path
    session = json.loads((raw / "session.json").read_text(encoding="utf-8"))
    events_file = raw / "events.jsonl"
    last_t = 0.0
    if events_file.exists():
        for line in events_file.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                last_t = max(last_t, float(json.loads(line).get("t", 0)))
            except ValueError:
                pass
    meta = {"binary": session.get("binary", "?"), "args": [], "started": session.get("started", raw.name),
            "duration_s": round(last_t, 1), "exit_code": session.get("exit_code"),
            "stopped_by": None if session.get("exit_code") is not None else "сеанс оборвался (вылет или программу закрыли силой)",
            "scenario": None, "software_gl": False, "hang_ms": session.get("hang_ms", 2000)}
    log_file = raw / "output.log"
    lines = log_file.read_text(encoding="utf-8", errors="replace").splitlines() if log_file.exists() else []
    if build:
        lines = name_windows_frames(lines, build if build.is_dir() else build.parent)
    r = analyze(meta, events_file, [(0.0, l) for l in lines], {}, Path(meta["binary"]), raw)
    head = [f"# Отчёт тестера — {meta['started'].replace('_', ' ')}", ""]
    words = raw / "описание.txt"
    if words.exists():
        head += ["**Что пишет тестер:**", "", "> " + words.read_text(encoding="utf-8", errors="replace").strip().replace("\n", "\n> "), ""]
    sysf = raw / "system.json"
    if sysf.exists():
        info = json.loads(sysf.read_text(encoding="utf-8"))
        head += ["**Компьютер и программа:**", "", "| | |", "|---|---|"]
        for group in ("program", "graphics", "computer"):
            for k, v in (info.get(group) or {}).items():
                head.append(f"| {group}.{k} | {cell(str(v))} |")
        head.append("")
    md = raw.parent / (raw.name + ".md")
    md.write_text("\n".join(head) + "\n" + render(r), encoding="utf-8")
    print(md)
    print(r["verdict"])
    return md


def main():
    p = argparse.ArgumentParser(description="BreezeCAD под наблюдением: отчёт о вылетах, зависаниях и просадках")
    p.add_argument("--bin", help="путь к breezecad(.exe) или к его папке (по умолчанию — свежая сборка в build/)")
    p.add_argument("--scenario", help="прогнать сценарий без человека (Linux): " + ", ".join(SCENARIOS) + " или путь к .sh")
    p.add_argument("--hang-ms", type=int, default=2000, help="порог зависания, мс (2000)")
    p.add_argument("--display", default=":99", help="дисплей Xvfb для сценария")
    p.add_argument("--settle", type=float, default=2.0, help="сколько ждать после сценария, с")
    p.add_argument("--report", help="только пересобрать отчёт из папки сырых данных")
    p.add_argument("--tester", help="отчёт тестера: zip из «Отчёты для разработчика» (или его распакованная папка)")
    p.add_argument("program_args", nargs="*", help="аргументы программы (после --)")
    args = p.parse_args()
    if args.tester:
        tester_report(Path(args.tester), Path(args.bin) if args.bin else None)
        return
    if args.report:  # re-analysis of an old run (after changing this script)
        raw = Path(args.report)
        meta = json.loads((raw.parent / (raw.name + ".json")).read_text(encoding="utf-8"))["meta"]
        out = [(0.0, l) for l in (raw / "output.log").read_text(encoding="utf-8", errors="replace").splitlines()]
        r = analyze(meta, raw / "events.jsonl", out, {}, Path(meta["binary"]), raw)
        (raw.parent / (raw.name + ".md")).write_text(render(r), encoding="utf-8")
        print(raw.parent / (raw.name + ".md"))
        return
    run(args)


if __name__ == "__main__":
    main()
