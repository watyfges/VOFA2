"""Adversarial load test for the VOFA2 remote-tuning server.

Sends garbage, half-open handshakes, absurd frame headers, floods and very slow
partial frames from several threads, then verifies that a clean client can still
connect, read a snapshot and round-trip a value.

Usage:
    python scripts/remote_stress.py --port 47900 [--seconds 20]
"""
import argparse
import base64
import hashlib
import json
import os
import random
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
stop_flag = threading.Event()
stats = {"sockets": 0, "bytes": 0, "frames": 0, "errors": 0}
watch_failures = {}
stats_lock = threading.Lock()


def bump(key, amount=1):
    with stats_lock:
        stats[key] += amount


def connect(host, port, timeout=3.0):
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    return sock


def ws_handshake(sock, host, port, key=None, version="13", extra=b""):
    key = key or base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET / HTTP/1.1\r\nHost: {host}:{port}\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: {version}\r\n"
    ).encode() + extra + b"\r\n"
    sock.sendall(request)
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            return False
        data += chunk
    return data.startswith(b"HTTP/1.1 101")


def frame(payload, opcode=0x1, masked=True, fin=True):
    header = bytearray([(0x80 if fin else 0) | opcode])
    length = len(payload)
    flag = 0x80 if masked else 0
    if length < 126:
        header.append(flag | length)
    elif length <= 0xFFFF:
        header.append(flag | 126)
        header += struct.pack("!H", length)
    else:
        header.append(flag | 127)
        header += struct.pack("!Q", length)
    if not masked:
        return bytes(header) + payload
    mask = os.urandom(4)
    return bytes(header) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


def fake_header(length, opcode=0x1):
    """A frame header claiming a huge body that is never sent."""
    header = bytearray([0x80 | opcode])
    if length < 126:
        header.append(0x80 | length)
    elif length <= 0xFFFF:
        header.append(0x80 | 126)
        header += struct.pack("!H", length)
    else:
        header.append(0x80 | 127)
        header += struct.pack("!Q", length)
    return bytes(header) + b"\x11\x22\x33\x44"


def garbage_worker(host, port):
    while not stop_flag.is_set():
        try:
            sock = connect(host, port)
            bump("sockets")
            for _ in range(random.randint(1, 4)):
                blob = os.urandom(random.randint(64, 8192))
                sock.sendall(blob)
                bump("bytes", len(blob))
            # RST instead of a clean close, like a phone losing Wi-Fi.
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            sock.close()
        except Exception:
            bump("errors")


def bad_handshake_worker(host, port):
    while not stop_flag.is_set():
        try:
            sock = connect(host, port)
            bump("sockets")
            choice = random.randint(0, 3)
            if choice == 0:
                sock.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n\r\n")
            elif choice == 1:
                ws_handshake(sock, host, port, version="8")
            elif choice == 2:
                sock.sendall(b"GET / HTTP/1.1\r\n" + b"X-Pad: " + b"A" * 12000)
            else:
                ws_handshake(sock, host, port, extra=b"X-Junk: " + os.urandom(2000) + b"\r\n")
            time.sleep(random.uniform(0.01, 0.08))
            sock.close()
        except Exception:
            bump("errors")


def frame_worker(host, port):
    while not stop_flag.is_set():
        try:
            sock = connect(host, port)
            bump("sockets")
            if not ws_handshake(sock, host, port):
                sock.close()
                continue
            for _ in range(random.randint(3, 12)):
                kind = random.randint(0, 4)
                if kind == 0:
                    sock.sendall(os.urandom(random.randint(1, 200)))
                elif kind == 1:
                    sock.sendall(fake_header(1 << random.randint(20, 40)))
                elif kind == 2:
                    sock.sendall(frame(os.urandom(random.randint(0, 300)), opcode=random.randint(0, 15)))
                elif kind == 3:
                    sock.sendall(frame(b'{"t":"value","id":1,"v":1}', masked=False))
                else:
                    sock.sendall(frame(b'{"t":"oneclick"}'))
                bump("frames")
                time.sleep(random.uniform(0.005, 0.03))
            sock.close()
        except Exception:
            bump("errors")


def flood_worker(host, port):
    while not stop_flag.is_set():
        try:
            sock = connect(host, port)
            bump("sockets")
            if not ws_handshake(sock, host, port):
                sock.close()
                continue
            sock.sendall(frame(b'{"t":"hello","clientId":"flood"}'))
            ping = frame(b'{"t":"ping"}')
            for _ in range(4000):
                if stop_flag.is_set():
                    break
                sock.sendall(ping)
                bump("frames")
            # The server should have closed us; drain whatever is left.
            try:
                while sock.recv(65536):
                    pass
            except Exception:
                pass
            sock.close()
        except Exception:
            bump("errors")


def slow_worker(host, port):
    """One byte every 300 ms: keeps a half-finished frame open under load."""
    while not stop_flag.is_set():
        try:
            sock = connect(host, port)
            bump("sockets")
            if not ws_handshake(sock, host, port):
                sock.close()
                continue
            payload = frame(b'{"t":"hello","clientId":"slow"}')
            for byte in payload:
                if stop_flag.is_set():
                    break
                sock.sendall(bytes([byte]))
                bump("bytes")
                time.sleep(0.3)
            sock.close()
        except Exception:
            bump("errors")


def watch_worker(host, port, samples, latencies):
    """A well-behaved client that must keep working during the attack."""
    while not stop_flag.is_set():
        try:
            started = time.time()
            sock = connect(host, port)
            if not ws_handshake(sock, host, port):
                raise RuntimeError("handshake failed")
            sock.sendall(frame(b'{"t":"hello","clientId":"watch","name":"watch"}'))
            sock.settimeout(3.0)
            data = b""
            while b'"t":"state"' not in data and time.time() - started < 3.0:
                data += sock.recv(65536)
            latencies.append((time.time() - started) * 1000.0)
            samples.append(len(data))
            sock.close()
        except Exception as exc:
            latencies.append(-1.0)
            samples.append(0)
            name = type(exc).__name__ + ":" + str(exc)[:40]
            watch_failures[name] = watch_failures.get(name, 0) + 1
        time.sleep(0.25)


def final_check(host, port):
    sock = connect(host, port, timeout=5.0)
    if not ws_handshake(sock, host, port):
        return False, "handshake failed"
    sock.sendall(frame(json.dumps({"t": "hello", "clientId": "final", "name": "验收"}).encode()))
    sock.settimeout(5.0)
    buffer = b""
    state = None
    deadline = time.time() + 5.0
    while time.time() < deadline:
        buffer += sock.recv(65536)
        while len(buffer) >= 2:
            opcode = buffer[0] & 0x0F
            length = buffer[1] & 0x7F
            offset = 2
            if length == 126:
                if len(buffer) < 4:
                    break
                length = struct.unpack("!H", buffer[2:4])[0]
                offset = 4
            elif length == 127:
                if len(buffer) < 10:
                    break
                length = struct.unpack("!Q", buffer[2:10])[0]
                offset = 10
            if len(buffer) < offset + length:
                break
            payload = buffer[offset:offset + length]
            buffer = buffer[offset + length:]
            if opcode == 0x1:
                message = json.loads(payload)
                if message["t"] == "state":
                    state = message
        if state:
            break

    if not state:
        return False, "no state after attack"

    control = state["controls"][0]
    target = control["min"] + 0.5
    sock.sendall(frame(json.dumps({
        "t": "commit", "id": control["id"], "v": target, "exact": False,
    }).encode()))

    buffer = b""
    deadline = time.time() + 5.0
    while time.time() < deadline:
        buffer += sock.recv(65536)
        if b'"t":"value"' in buffer:
            sock.close()
            return True, f"state controls={len(state['controls'])} value={target}"
    sock.close()
    return False, "no value echo after attack"


def rss_mb(pid):
    try:
        import ctypes
        from ctypes import wintypes

        class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD),
                ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        counters = PROCESS_MEMORY_COUNTERS()
        counters.cb = ctypes.sizeof(counters)
        handle = ctypes.windll.kernel32.OpenProcess(0x1000 | 0x0400, False, pid)
        if not handle:
            return None
        ok = ctypes.windll.psapi.GetProcessMemoryInfo(
            handle, ctypes.byref(counters), ctypes.sizeof(counters))
        ctypes.windll.kernel32.CloseHandle(handle)
        return counters.WorkingSetSize / (1024 * 1024) if ok else None
    except Exception:
        return None


def snapshot_config():
    """The app persists settings on exit; keep the user's workspace untouched."""
    base = os.path.join(os.environ.get("LOCALAPPDATA", ""), "VOFA2")
    files = ["workspace.json", "workspace.autosave.json", "workspace.bak"]
    backup = {}
    for name in files:
        path = os.path.join(base, name)
        if os.path.exists(path):
            with open(path, "rb") as handle:
                backup[name] = handle.read()
        else:
            backup[name] = None
    return base, backup


def restore_config(base, backup):
    for name, content in backup.items():
        path = os.path.join(base, name)
        if content is None:
            if os.path.exists(path):
                os.remove(path)
        else:
            with open(path, "wb") as handle:
                handle.write(content)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=47900)
    parser.add_argument("--seconds", type=float, default=20.0)
    parser.add_argument("--pid", type=int, default=0, help="app PID for RSS sampling")
    parser.add_argument("--launch", default="", help="path to vofa2_app.exe to spawn")
    parser.add_argument("--profile", choices=["harsh", "normal"], default="harsh",
                        help="harsh: many attackers at once; normal: one device poking")
    args = parser.parse_args()

    config_base = None
    config_backup = None
    proc = None
    if args.launch:
        config_base, config_backup = snapshot_config()
        proc = subprocess.Popen(
            [args.launch, "--remote", "--remote-port", str(args.port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        args.pid = proc.pid
        time.sleep(4.0)
        if proc.poll() is not None:
            print(f"app exited during startup (code {proc.returncode})")
            restore_config(config_base, config_backup)
            return 1

    try:
        return run(args)
    finally:
        if proc is not None:
            alive = proc.poll() is None
            if alive:
                proc.terminate()
            try:
                code = proc.wait(10)
            except subprocess.TimeoutExpired:
                proc.kill()
                code = proc.wait(5)
            print(f"app was alive after attack: {alive} (exit {code})")
            restore_config(config_base, config_backup)


def run(args):
    before = rss_mb(args.pid) if args.pid else None
    samples, latencies = [], []

    harsh = args.profile == "harsh"
    threads = []
    for _ in range(3 if harsh else 1):
        threads.append(threading.Thread(target=garbage_worker, args=(args.host, args.port), daemon=True))
    for _ in range(2 if harsh else 1):
        threads.append(threading.Thread(target=bad_handshake_worker, args=(args.host, args.port), daemon=True))
    for _ in range(3 if harsh else 1):
        threads.append(threading.Thread(target=frame_worker, args=(args.host, args.port), daemon=True))
    if harsh:
        threads.append(threading.Thread(target=flood_worker, args=(args.host, args.port), daemon=True))
    threads.append(threading.Thread(target=slow_worker, args=(args.host, args.port), daemon=True))
    threads.append(threading.Thread(target=watch_worker, args=(args.host, args.port, samples, latencies), daemon=True))

    for thread in threads:
        thread.start()

    peak = before or 0.0
    deadline = time.time() + args.seconds
    while time.time() < deadline:
        if args.pid:
            current = rss_mb(args.pid)
            if current:
                peak = max(peak, current)
        time.sleep(0.5)

    stop_flag.set()
    time.sleep(1.5)

    after = rss_mb(args.pid) if args.pid else None
    ok, detail = final_check(args.host, args.port)

    good = [value for value in latencies if value > 0]
    print(f"connections={stats['sockets']} frames={stats['frames']} "
          f"bytes={stats['bytes']} errors={stats['errors']}")
    if good:
        good.sort()
        print(f"watchdog samples={len(latencies)} ok={len(good)} "
              f"p50={good[len(good) // 2]:.0f}ms max={good[-1]:.0f}ms")
    else:
        print(f"watchdog samples={len(latencies)} ok=0")
    if watch_failures:
        print("watchdog failures:", watch_failures)
    if before:
        print(f"rss before={before:.1f}MB peak={peak:.1f}MB after={after:.1f}MB")
    print(f"final check: {'OK' if ok else 'FAILED'} ({detail})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
