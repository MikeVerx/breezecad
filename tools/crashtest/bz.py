#!/usr/bin/env python3
"""Tiny MCP client for a running BreezeCAD (the mcp module), for scripted checks.

    from bz import call
    call("assembly.new")
"""
import json
import os
import sys
import urllib.request

CONF = os.path.expanduser("~/.config/breezecad/mcp/connection.json")
_session = {"id": None, "n": 0}


def _post(payload):
    conf = json.load(open(CONF))
    req = urllib.request.Request(conf["url"], data=json.dumps(payload).encode(), method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("Accept", "application/json, text/event-stream")
    req.add_header("Authorization", "Bearer " + conf["token"])
    if _session["id"]:
        req.add_header("Mcp-Session-Id", _session["id"])
    with urllib.request.urlopen(req, timeout=120) as r:
        sid = r.headers.get("Mcp-Session-Id")
        if sid:
            _session["id"] = sid
        body = r.read().decode()
    if not body.strip():
        return None
    if body.lstrip().startswith("event:") or body.lstrip().startswith("data:"):
        for line in body.splitlines():
            if line.startswith("data:"):
                body = line[5:]
    return json.loads(body)


def _rpc(method, params=None):
    _session["n"] += 1
    return _post({"jsonrpc": "2.0", "id": _session["n"], "method": method, "params": params or {}})


def _init():
    if _session["id"] is None and _session["n"] == 0:
        _rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "bz.py", "version": "1"}})
        _post({"jsonrpc": "2.0", "method": "notifications/initialized"})


def call(command, **args):
    """Runs a BreezeCAD command (a dot name) and returns its JSON result; raises on an error."""
    _init()
    r = _rpc("tools/call", {"name": command.replace(".", "_"), "arguments": args})
    if r is None:
        raise RuntimeError("no answer")
    if "error" in r:
        raise RuntimeError(f"{command}: {r['error']}")
    res = r["result"]
    text = "".join(c.get("text", "") for c in res.get("content", []) if c.get("type") == "text")
    if res.get("isError"):
        raise RuntimeError(f"{command}: {text}")
    try:
        return json.loads(text)
    except Exception:
        return text


def tools():
    _init()
    return [t["name"] for t in _rpc("tools/list")["result"]["tools"]]


if __name__ == "__main__":
    print(json.dumps(call(sys.argv[1], **(json.loads(sys.argv[2]) if len(sys.argv) > 2 else {})), ensure_ascii=False, indent=1))
