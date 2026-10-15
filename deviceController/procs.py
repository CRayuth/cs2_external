import ctypes
from ctypes import wintypes
import os
import subprocess
import threading

TH32CS_SNAPPROCESS = 0x00000002
PROCESS_TERMINATE = 0x0001
PROCESS_QUERY_INFORMATION = 0x0400
STILL_ACTIVE = 259

INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", ctypes.c_wchar * 260),
    ]


_kernel32 = ctypes.windll.kernel32
_kernel32.CreateToolhelp32Snapshot.restype = ctypes.c_void_p
_kernel32.OpenProcess.restype = wintypes.HANDLE


def find_process(name):
    target = name.lower()
    snapshot = _kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snapshot is None or snapshot == INVALID_HANDLE_VALUE:
        return None
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
    snapshot_handle = ctypes.c_void_p(snapshot)
    found = None
    try:
        if not _kernel32.Process32FirstW(snapshot_handle, ctypes.byref(entry)):
            return None
        while True:
            if entry.szExeFile.lower() == target:
                found = entry.th32ProcessID
                break
            if not _kernel32.Process32NextW(snapshot_handle, ctypes.byref(entry)):
                break
    finally:
        _kernel32.CloseHandle(snapshot_handle)
    return found


class VestaLauncher:
    def __init__(self, vesta_path):
        self.vesta_path = vesta_path
        self.handle = None
        self.process = None
        self._lock = threading.Lock()

    def _resolve_path(self):
        raw = os.path.expandvars(os.path.expanduser(self.vesta_path or ""))
        if raw and os.path.isfile(raw):
            return raw
        return None

    def path_ok(self):
        return self._resolve_path() is not None

    def _handle_alive(self):
        code = wintypes.DWORD()
        if _kernel32.GetExitCodeProcess(self.handle, ctypes.byref(code)):
            return code.value == STILL_ACTIVE
        return False

    def running(self):
        with self._lock:
            if self.handle is not None:
                if self._handle_alive():
                    return True
                _kernel32.CloseHandle(self.handle)
                self.handle = None
                self.process = None
            return find_process("vesta.exe") is not None

    def start(self):
        with self._lock:
            if self.handle is not None and self._handle_alive():
                return {"ok": True, "reason": "already-running"}
            if find_process("vesta.exe") is not None:
                return {"ok": True, "reason": "already-running"}
            path = self._resolve_path()
            if not path:
                return {"ok": False, "reason": "not-found", "path": self.vesta_path}
            try:
                creation = subprocess.Popen([path], cwd=os.path.dirname(path), close_fds=True)
            except OSError as error:
                return {"ok": False, "reason": "launch-failed", "error": str(error)}
            self.process = creation
            self.handle = _kernel32.OpenProcess(
                PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION, False, creation.pid)
            return {"ok": True, "reason": "started", "pid": creation.pid, "path": path}

    def stop(self):
        with self._lock:
            stopped = False
            if self.process is not None and self.process.poll() is None:
                self.process.terminate()
                stopped = True
            if self.handle is not None:
                if self._handle_alive():
                    _kernel32.TerminateProcess(self.handle, 0)
                    stopped = True
                _kernel32.CloseHandle(self.handle)
                self.handle = None
            self.process = None
            return {"ok": True, "reason": "stopped" if stopped else "not-running"}


class ProcessWatcher(threading.Thread):
    def __init__(self, vesta_launcher, interval=0.7):
        super().__init__(daemon=True)
        self.vesta = vesta_launcher
        self.interval = interval
        self.auto_launch = True
        self.last_action = ""
        self._stop = threading.Event()
        self._launch_pending = True

    def stop(self):
        self._stop.set()

    def run(self):
        while not self._stop.wait(self.interval):
            try:
                self._tick()
            except Exception:
                pass

    def _tick(self):
        game_up = find_process("cs2.exe") is not None
        if not game_up:
            self._launch_pending = True
            return
        if self.auto_launch and self._launch_pending and not self.vesta.running():
            result = self.vesta.start()
            self.last_action = "auto-launch: %s" % result.get("reason", "")
            if result.get("ok"):
                self._launch_pending = False
