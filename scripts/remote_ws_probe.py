"""Minimal WebSocket client used to validate the VOFA2 remote tuning server.

Deliberately dependency free: it speaks RFC 6455 by hand so it is an
independent check of the C++ implementation (not a re-use of it).
"""
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time

HOST = os.environ.get("VOFA_HOST", "127.0.0.1")
PORT = int(os.environ.get("VOFA_PORT", "47900"))


def handshake(sock):
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(request.encode())
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("server closed during handshake")
        data += chunk
    header, rest = data.split(b"\r\n\r\n", 1)
    text = header.decode("latin1")
    assert text.startswith("HTTP/1.1 101"), text
    expected = base64.b64encode(
        hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
    ).decode()
    assert f"Sec-WebSocket-Accept: {expected}" in text, text
    print("[ws] handshake ok")
    return rest


def send_text(sock, payload):
    data = payload.encode()
    mask = os.urandom(4)
    header = bytearray([0x81])
    length = len(data)
    if length < 126:
        header.append(0x80 | length)
    elif length <= 0xFFFF:
        header.append(0x80 | 126)
        header += struct.pack("!H", length)
    else:
        header.append(0x80 | 127)
        header += struct.pack("!Q", length)
    header += mask
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    sock.sendall(bytes(header) + masked)


def read_frames(sock, initial=b""):
    buffer = initial
    frames = []
    sock.settimeout(0.4)
    deadline = time.time() + 2.0
    while time.time() < deadline:
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
                frames.append(payload.decode())
            elif opcode == 0x8:
                return frames, b"", True
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            return frames, b"", True
        buffer += chunk
    return frames, buffer, False


def main():
    sock = socket.create_connection((HOST, PORT), timeout=5)
    rest = handshake(sock)

    send_text(sock, json.dumps({
        "t": "hello", "proto": 1, "clientId": "py-validator", "name": "Python 校验",
    }))
    frames, rest, closed = read_frames(sock, rest)
    kinds = []
    state = None
    for frame in frames:
        message = json.loads(frame)
        kinds.append(message["t"])
        if message["t"] == "state":
            state = message
    print("[ws] received:", kinds)
    assert "welcome" in kinds and state is not None, kinds

    controls = state["controls"]
    print(f"[ws] server={state['server']} groups={state['groups']} controls={len(controls)}")
    assert controls, "no controls in snapshot"
    control = controls[0]

    send_text(sock, json.dumps({
        "t": "commit", "id": control["id"], "v": control["min"] + 0.25, "exact": False,
    }))
    frames, rest, closed = read_frames(sock, rest)
    echoes = [json.loads(f) for f in frames if json.loads(f)["t"] == "value"]
    print("[ws] echo:", echoes)
    assert echoes and abs(echoes[0]["v"] - (control["min"] + 0.25)) < 1e-9

    # Stay connected so the PC dialog shows one client.
    hold = float(os.environ.get("VOFA_HOLD", "4"))
    deadline = time.time() + hold
    while time.time() < deadline:
        frames, rest, closed = read_frames(sock, rest)
        for frame in frames:
            message = json.loads(frame)
            if message["t"] == "state":
                print("[ws] state refresh seq", message.get("seq"))
        if closed:
            break
    sock.close()
    print("[ws] done")


if __name__ == "__main__":
    main()
