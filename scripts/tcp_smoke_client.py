#!/usr/bin/env python3
"""Small Windows/WSL smoke-test peer for VOFA2.

--client PORT: connect to the app's TCP server, send JustFloat frames for a few
seconds, then print any one-click command bytes received from the app.

--server PORT: listen for the app's TCP client, send JustFloat frames, and print
anything received.
"""

from __future__ import annotations

import argparse
import math
import select
import socket
import struct
import time

TAIL = b"\x00\x00\x80\x7f"


def just_float_frame(t: float) -> bytes:
    values = [
        math.sin(t),
        math.sin(2.0 * t),
        math.cos(t) * 2.0,
        math.sin(0.25 * t) * 3.0,
    ]
    return struct.pack("<4f", *values) + TAIL


def bmp_frame(width: int, height: int, phase: float) -> bytes:
    """Uncompressed 24-bit BMP, generated without external dependencies."""
    row_size = ((width * 3 + 3) // 4) * 4
    image_size = row_size * height
    header = struct.pack("<2sIHHI", b"BM", 54 + image_size, 0, 0, 54)
    dib = struct.pack(
        "<IiiHHIIiiII", 40, width, height, 1, 24, 0, image_size, 2835, 2835, 0, 0
    )

    rows = []
    for y in range(height):
        row = bytearray(row_size)
        for x in range(width):
            # Moving colour bands make it obvious that frames are updating.
            r = int(127 + 127 * math.sin(phase + x * 0.05))
            g = int(127 + 127 * math.sin(phase * 1.3 + y * 0.08))
            b = int(127 + 127 * math.sin(phase * 0.7 + (x + y) * 0.04))
            offset = x * 3
            row[offset + 0] = b
            row[offset + 1] = g
            row[offset + 2] = r
        rows.append(bytes(row))
    return header + dib + b"".join(reversed(rows))


def image_frame(width: int, height: int, phase: float) -> bytes:
    data = bmp_frame(width, height, phase)
    head = f"image:0,25,{width},{height},{len(data)}\n".encode("ascii")
    return head + data


def run_client(port: int, seconds: float, send_image: bool) -> None:
    sock = None
    connect_deadline = time.time() + 20
    while sock is None:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        except OSError:
            if time.time() > connect_deadline:
                raise
            time.sleep(0.2)

    with sock:
        sock.settimeout(0.2)
        print(f"connected to 127.0.0.1:{port}")

        received = bytearray()
        deadline = time.time() + seconds
        start = time.time()
        last_image = -1.0

        while time.time() < deadline:
            t = time.time() - start
            try:
                sock.sendall(just_float_frame(t))
                if send_image and t - last_image >= 0.4:
                    last_image = t
                    sock.sendall(image_frame(160, 90, t))
            except (BrokenPipeError, ConnectionResetError):
                break
            if select.select([sock], [], [], 0)[0]:
                try:
                    chunk = sock.recv(4096)
                except (ConnectionResetError, OSError):
                    break
                if chunk:
                    received.extend(chunk)
            time.sleep(0.005)

        # Drain any remaining one-click command bytes.
        end = time.time() + 1.5
        while time.time() < end:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                received.extend(chunk)
            except socket.timeout:
                pass

        print("frames sent for %.1fs" % seconds)
        print("received from app:", bytes(received))


def run_server(port: int, seconds: float, send_image: bool) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", port))
        server.listen(1)
        print(f"listening on 127.0.0.1:{port}")
        conn, addr = server.accept()
        print("accepted", addr)
        with conn:
            conn.settimeout(0.2)
            start = time.time()
            deadline = start + seconds
            received = bytearray()
            last_image = -1.0
            while time.time() < deadline:
                t = time.time() - start
                try:
                    conn.sendall(just_float_frame(t))
                    if send_image and t - last_image >= 0.4:
                        last_image = t
                        conn.sendall(image_frame(160, 90, t))
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    break
                if select.select([conn], [], [], 0)[0]:
                    try:
                        chunk = conn.recv(4096)
                    except OSError:
                        break
                    if chunk:
                        received.extend(chunk)
                time.sleep(0.005)
            print("received from app:", bytes(received))
            print("hex:", bytes(received).hex())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=int)
    parser.add_argument("--server", type=int)
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--image", action="store_true", help="also send image frames")
    args = parser.parse_args()

    if args.client is not None:
        run_client(args.client, args.seconds, args.image)
        return 0
    if args.server is not None:
        run_server(args.server, args.seconds, args.image)
        return 0
    parser.error("choose --client or --server")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
