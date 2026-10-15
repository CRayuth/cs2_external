import json
import os
import shutil

_HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(_HERE, "config.json")
SPEC_PATH = os.path.join(_HERE, "spec.json")
PACKAGE_SRC = os.path.join(_HERE, "vesta-package")

EXCLUDE_TOKENS = ()


def excluded(path):
    lowered = str(path).lower()
    return any(token in lowered for token in EXCLUDE_TOKENS)

_DEFAULTS = {
    "vesta_path": "",
    "host": "127.0.0.1",
    "port": 8760,
    "auto_launch": True,
    "watch_interval_ms": 700,
    "package_id": "vesta_device_controller",
    "install_package": True,
    "enable_autoload": True,
}


def load_config(path=None):
    config_path = path or CONFIG_PATH
    with open(config_path, "r", encoding="utf-8-sig") as handle:
        config = json.load(handle)
    for key, value in _DEFAULTS.items():
        config.setdefault(key, value)
    return config


def lua_root():
    base = os.environ.get("TEMP") or os.environ.get("TMP") or os.path.expanduser("~")
    return os.path.join(base, "vesta", "lua")


def scripts_root():
    return os.path.join(lua_root(), "scripts")


def data_dir(package_id):
    return os.path.join(lua_root(), "data", package_id)


def package_dest(package_id):
    return os.path.join(scripts_root(), package_id)


def live_path(package_id):
    return os.path.join(data_dir(package_id), "live.json")


def request_path(package_id):
    return os.path.join(data_dir(package_id), "request.json")


def install_package(package_id):
    destination = package_dest(package_id)
    if not os.path.isdir(PACKAGE_SRC):
        return {"ok": False, "reason": "source-missing", "src": PACKAGE_SRC}
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    shutil.copytree(PACKAGE_SRC, destination, dirs_exist_ok=True)
    return {"ok": True, "dest": destination}


def enable_autoload(package_id):
    runtime_json = os.path.join(lua_root(), "runtime.json")
    document = {}
    if os.path.isfile(runtime_json):
        try:
            with open(runtime_json, "r", encoding="utf-8-sig") as handle:
                loaded = json.load(handle)
            if isinstance(loaded, dict):
                document = loaded
        except (ValueError, OSError):
            document = {}
    entry = document.get(package_id)
    entry = entry if isinstance(entry, dict) else {}
    entry["autoload"] = True
    entry.setdefault("hot_reload", True)
    document[package_id] = entry
    os.makedirs(lua_root(), exist_ok=True)
    temporary = runtime_json + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump(document, handle)
    os.replace(temporary, runtime_json)
    return {"ok": True, "path": runtime_json}


def read_live(package_id):
    path = live_path(package_id)
    if not os.path.isfile(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (ValueError, OSError):
        return None


def write_request(package_id, revision, edits):
    directory = data_dir(package_id)
    os.makedirs(directory, exist_ok=True)
    path = request_path(package_id)
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump({"rev": int(revision), "edits": edits}, handle)
    os.replace(temporary, path)
    return path


def load_spec(path=None):
    with open(path or SPEC_PATH, "r", encoding="utf-8-sig") as handle:
        spec = json.load(handle)
    spec["pages"] = [page for page in spec.get("pages", [])]
    for page in spec["pages"]:
        for tab in page.get("tabs", []):
            tab["cards"] = [c for c in tab.get("cards", []) if not excluded(c.get("title", ""))]
    return spec


def sanitize_values(values):
    clean = []
    for item in values or []:
        if not isinstance(item, dict):
            continue
        if excluded(item.get("path", "")):
            continue
        clean.append(item)
    return clean
