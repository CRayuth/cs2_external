import argparse
import json
import os
import socket
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import procs
import settings

_WEB_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
_STATIC_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".ico": "image/x-icon",
    ".json": "application/json; charset=utf-8",
    ".svg": "image/svg+xml",
}

SECTION_LABELS = {
    "combat_global": "Combat",
    "combat_overrides": "Weapon Overrides",
    "esp": "Visuals",
    "misc": "Misc",
}


def local_ips():
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    probe = None
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        ips.add(probe.getsockname()[0])
    except OSError:
        pass
    finally:
        if probe is not None:
            probe.close()
    return sorted(ip for ip in ips if not ip.startswith("127."))


class Controller:
    def __init__(self, config):
        self.config = config
        self.package_id = config["package_id"]
        self.launcher = procs.VestaLauncher(config.get("vesta_path", ""))
        interval = max(0.2, float(config.get("watch_interval_ms", 700)) / 1000.0)
        self.watcher = procs.ProcessWatcher(self.launcher, interval)
        self.watcher.auto_launch = bool(config.get("auto_launch", True))
        self.install_report = None
        self.autoload_report = None
        self.pending_rev = 0
        try:
            self.spec = settings.load_spec()
        except (OSError, ValueError) as error:
            self.spec = {"pages": [], "error": str(error)}

    def prepare(self):
        if self.config.get("install_package", True):
            self.install_report = settings.install_package(self.package_id)
        if self.config.get("enable_autoload", True):
            self.autoload_report = settings.enable_autoload(self.package_id)

    def state(self):
        live = settings.read_live(self.package_id) or {}
        values = settings.sanitize_values(live.get("values"))
        applied = live.get("applied", 0) or 0
        stamp = live.get("ts", 0) or 0
        now_ms = int(time.time() * 1000)
        age = max(0, now_ms - stamp) if stamp else None
        connected = bool(values) and age is not None and age < 2500
        synced = self.pending_rev == 0 or applied == self.pending_rev
        return {
            "ok": bool(live),
            "connected": connected,
            "synced": synced,
            "bridge": {
                "seq": live.get("seq"),
                "applied": applied,
                "pending": self.pending_rev,
                "age_ms": age,
                "count": len(values),
            },
            "values": values,
            "sections": SECTION_LABELS,
            "game_running": procs.find_process("cs2.exe") is not None,
            "vesta_running": self.launcher.running(),
            "vesta_path_ok": self.launcher.path_ok(),
            "vesta_path": self.config.get("vesta_path", ""),
            "auto_launch": self.watcher.auto_launch,
            "last_action": self.watcher.last_action,
            "install_ok": bool(self.install_report and self.install_report.get("ok")),
            "autoload_ok": bool(self.autoload_report and self.autoload_report.get("ok")),
            "data_dir": settings.data_dir(self.package_id),
            "host": self.config.get("host", "127.0.0.1"),
            "port": self.config.get("port", 8760),
            "ips": local_ips(),
        }

    def apply(self, edits):
        clean = []
        for edit in edits or []:
            if isinstance(edit, dict) and edit.get("path"):
                clean.append({"path": str(edit["path"]), "value": edit.get("value")})
        if not clean:
            return {"ok": False, "reason": "empty"}
        revision = int(time.time() * 1_000_000)
        self.pending_rev = revision
        settings.write_request(self.package_id, revision, clean)
        return {"ok": True, "rev": revision, "count": len(clean)}

    def vesta(self, action):
        if action == "start":
            return self.launcher.start()
        if action == "stop":
            return self.launcher.stop()
        return {"ok": False, "reason": "unknown-action"}

    def set_auto(self, enabled):
        self.watcher.auto_launch = bool(enabled)
        if enabled:
            self.watcher._launch_pending = True
        return {"ok": True, "auto_launch": self.watcher.auto_launch}

    def reinstall(self):
        self.install_report = settings.install_package(self.package_id)
        self.autoload_report = settings.enable_autoload(self.package_id)
        try:
            self.spec = settings.load_spec()
        except (OSError, ValueError) as error:
            self.spec = {"pages": [], "error": str(error)}
        return {"ok": bool(self.install_report.get("ok")), "autoload": bool(self.autoload_report.get("ok"))}


class Handler(BaseHTTPRequestHandler):
    controller = None
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        return

    def _send_json(self, payload, status=HTTPStatus.OK):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_static(self, path):
        safe = os.path.normpath(path).lstrip("/\\")
        full = os.path.abspath(os.path.join(_WEB_ROOT, safe))
        if not full.startswith(os.path.abspath(_WEB_ROOT)) or not os.path.isfile(full):
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        ext = os.path.splitext(full)[1].lower()
        with open(full, "rb") as handle:
            body = handle.read()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", _STATIC_TYPES.get(ext, "application/octet-stream"))
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0:
            return {}
        raw = self.rfile.read(length)
        try:
            return json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            return {}

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/api/state":
            self._send_json(self.controller.state())
            return
        if path == "/api/spec":
            self._send_json(self.controller.spec)
            return
        if path in ("/", "/index.html"):
            self._send_static("index.html")
            return
        if path.startswith("/api/"):
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        self._send_static(path)

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        body = self._read_body()
        if path == "/api/apply":
            self._send_json(self.controller.apply(body.get("edits")))
            return
        if path == "/api/vesta":
            self._send_json(self.controller.vesta(body.get("action")))
            return
        if path == "/api/auto":
            self._send_json(self.controller.set_auto(body.get("enabled")))
            return
        if path == "/api/install":
            self._send_json(self.controller.reinstall())
            return
        self.send_error(HTTPStatus.NOT_FOUND)


def run(config_path=None, host=None, port=None):
    config = settings.load_config(config_path)
    if host is not None:
        config["host"] = host
    if port is not None:
        config["port"] = port
    controller = Controller(config)
    controller.prepare()
    bind = config.get("host", "127.0.0.1")
    listen_port = int(config.get("port", 8760))
    Handler.controller = controller
    controller.watcher.start()
    server = ThreadingHTTPServer((bind, listen_port), Handler)
    server.daemon_threads = True
    urls = ["http://127.0.0.1:%d" % listen_port]
    for ip in local_ips():
        urls.append("http://%s:%d" % (ip, listen_port))
    print("Vesta Device Controller (full-feature web panel)")
    print("Bridge data dir: " + settings.data_dir(controller.package_id))
    if controller.install_report:
        print("Package install: " + json.dumps(controller.install_report))
    if controller.autoload_report:
        print("Autoload: " + json.dumps(controller.autoload_report))
    print("Open in any browser on the LAN:")
    for url in urls:
        print("  " + url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        controller.watcher.stop()
        server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Vesta full-feature LAN web controller")
    parser.add_argument("--config", default=None, help="path to config.json")
    parser.add_argument("--lan", action="store_true", help="bind to 0.0.0.0 so other devices can connect")
    parser.add_argument("--host", default=None, help="explicit bind address (overrides --lan/config)")
    parser.add_argument("--port", type=int, default=None, help="explicit port (overrides config.json)")
    args = parser.parse_args()
    run(args.config, ("0.0.0.0" if args.lan else args.host), args.port)
