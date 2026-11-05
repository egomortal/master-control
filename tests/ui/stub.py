#!/usr/bin/env python3
"""A stand-in for master-control.elf's web server, for testing the menu in a desktop browser.
Serves web/index.html, web/classic.html, web/overlay.src.html (at /overlay), assets/icon0.png and /api/health, answers the same /api/ paths,
requires the X-PadBridge header on POSTs like the payload, and records every POST.
GET /__mode?m=recorded|rich|btfail|down switches what /api/state returns.
'recorded' is the console's real /api/state read on 2026-11-02 (test/recorded-state.json)."""
import json, os, sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.path.dirname(os.path.abspath(__file__))
while not os.path.exists(os.path.join(ROOT, "web", "index.html")) and os.path.dirname(ROOT) != ROOT:
    ROOT = os.path.dirname(ROOT)            # the source root, from test/ or tests/ui/
HERE = os.path.dirname(os.path.abspath(__file__))
STATE = {"mode": "rich", "posts": [], "pair_until": 0, "lang": "en"}
RECORDED = json.load(open(os.path.join(HERE, "recorded-state.json")))


def pad(slot, transport, model, vid, pid, addr, virtual="ready", battery=-1, buttons=()):
    return {"slot": slot, "transport": transport, "profile": "USB" if transport == "usb" else "XboxBLE", "model": model,
            "vid": vid, "pid": pid, "addr": addr, "virtual": virtual, "battery": battery, "charging": False,
            "lx": 128, "ly": 128, "rx": 128, "ry": 128, "l2": 0, "r2": 0, "buttons": list(buttons)}


def state():
    m = STATE["mode"]
    if m == "recorded":
        s = json.loads(json.dumps(RECORDED))
    else:
        s = {"version": "0.4.4", "running": True, "pairing": max(0, int(STATE["pair_until"] - time.time())), "bluetooth": "ok", "reason": "", "reasonKey": "",
             "lang": STATE["lang"], "combo": "", "physical": {"readable": False, "error": "00000000", "buttons": []},
             "pads": [pad(1, "bt", "Xbox Series X|S", "045e", "0b13", "02:00:00:00:00:01", battery=55, buttons=["cross"]),
                      pad(5, "usb", "Razer Wolverine Ultimate", "1532", "0a14", "ugen0.4"),
                      pad(6, "usb", "GameSir G7 SE", "3537", "1082", "ugen0.5"),
                      pad(7, "usb", "Switch Pro Controller", "057e", "2009", "ugen0.6", virtual="waiting")],
             "paired": [{"addr": "02:00:00:00:00:01", "vid": "045e", "pid": "0b13", "le": True, "connected": True},
                        {"addr": "AC:FD:93:11:22:33", "vid": "054c", "pid": "09cc", "le": False, "connected": False}]}
        if m == "btfail":
            s["bluetooth"] = "failed"; s["reasonKey"] = "nochip"; s["pads"] = [p for p in s["pads"] if p["transport"] == "usb"]
    s["lang"] = STATE["lang"]
    return s


REMAP = {"devices": [
    {"dev": "045E_0B13", "vid": "045e", "pid": "0b13", "name": "Xbox Series X|S", "seen": True, "saved": True,
     "map": {"cross": "circle", "circle": "cross", "square": "square", "triangle": "triangle", "l1": "l1", "r1": "r1", "l2": "l2", "r2": "r2",
             "l3": "l3", "r3": "r3", "touchpad": "touchpad", "options": "options", "create": "create", "ps": "ps",
             "up": "up", "down": "down", "left": "left", "right": "right", "p1": "none", "p2": "none", "p3": "none", "p4": "none", "m5": "none", "m6": "none"},
     "swap_sticks": False, "invert_ly": False, "invert_ry": True, "swap_triggers": False, "hair_l2": False, "hair_r2": False},
    {"dev": "1532_0A14", "vid": "1532", "pid": "0a14", "name": "Razer Wolverine Ultimate", "seen": True, "saved": False,
     "map": {}, "swap_sticks": False, "invert_ly": False, "invert_ry": False, "swap_triggers": False, "hair_l2": False, "hair_r2": False}]}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, ctype, body):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/__mode":
            STATE["mode"] = parse_qs(u.query)["m"][0]; return self.send(200, "text/plain", "ok")
        if u.path == "/__posts":
            return self.send(200, "application/json", json.dumps(STATE["posts"]))
        if u.path in ("/", "/index.html"):
            return self.send(200, "text/html; charset=utf-8", open(os.path.join(ROOT, "web", "index.html"), "rb").read())
        if u.path == "/classic":
            return self.send(200, "text/html; charset=utf-8", open(os.path.join(ROOT, "web", "classic.html"), "rb").read())
        if u.path == "/overlay":
            return self.send(200, "text/html; charset=utf-8", open(os.path.join(ROOT, "web", "overlay.src.html"), "rb").read())
        if u.path == "/tools" or u.path.startswith("/tools/"):
            return self.tools(u.path)
        if u.path == "/api/health" and STATE["mode"] != "down":
            return self.send(200, "application/json", '{"ok":true,"version":"0.4.4"}')
        if u.path == "/icon.png":
            return self.send(200, "image/png", open(os.path.join(ROOT, "assets", "icon0.png"), "rb").read())
        if STATE["mode"] == "down" and u.path.startswith("/api/"):
            return self.send(503, "application/json", "{}")
        if u.path == "/api/state":
            return self.send(200, "application/json", json.dumps(state()))
        if u.path == "/api/remap":
            return self.send(200, "application/json", json.dumps(REMAP if STATE["mode"] != "recorded" else {"devices": []}))
        if u.path in ("/api/log", "/api/logfull"):
            return self.send(200, "text/plain", "master-control 0.4.4 started\nweb: listening on port 8766\n")
        return self.send(404, "application/json", '{"error":"not found"}')

    def tools(self, path):
        """Same rules as the payload's table: exact files under web/tools, dir/ -> index.html, dir -> 301 dir/."""
        base = os.path.realpath(os.path.join(ROOT, "web", "tools"))
        rel = path[len("/tools"):].lstrip("/")
        full = os.path.realpath(os.path.join(base, rel))
        if full != base and not full.startswith(base + os.sep):
            return self.send(404, "application/json", '{"error":"not found"}')
        if os.path.isdir(full):
            if not path.endswith("/"):
                self.send_response(301); self.send_header("Location", path + "/"); self.send_header("Content-Length", "0"); self.end_headers(); return
            full = os.path.join(full, "index.html")
        if ".." in path.split("/") or not os.path.isfile(full):
            return self.send(404, "application/json", '{"error":"not found"}')
        ext = os.path.splitext(full)[1]
        mime = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
                ".woff2": "font/woff2", ".txt": "text/plain; charset=utf-8"}.get(ext, "application/octet-stream")
        return self.send(200, mime, open(full, "rb").read())

    def do_POST(self):
        u = urlparse(self.path)
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n).decode() if n else ""
        if self.headers.get("X-PadBridge") != "1":
            return self.send(403, "application/json", '{"error":"forbidden"}')
        STATE["posts"].append({"path": self.path, "body": body})
        if u.path == "/api/pair":
            STATE["pair_until"] = time.time() + 6
        if u.path == "/api/lang":
            STATE["lang"] = parse_qs(u.query)["code"][0]
        return self.send(200, "application/json", '{"ok":true}')


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 8766), H).serve_forever()
