#!/usr/bin/env python3
"""TCP peer for VOFA2 soak tests.

Streams 8-channel JustFloat frames, injects malformed/truncated bytes, and
keeps the connection alive for the configured duration. Usage:

    python3 scripts/soak_peer.py --port 23123 --seconds 120
"""

import argparse
import math
import os
import random
import socket
import struct
import time

JUSTFLOAT_TAIL = b"\x00\x00\x80\x7f"


def frame(values):
    return struct.pack("<{}f".format(len(values)), *values) + JUSTFLOAT_TAIL


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=23123)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--rate", type=float, default=200.0)
    args = parser.parse_args()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", args.port))
    server.listen(1)
    server.settimeout(30)

    print("soak peer listening on", args.port, flush=True)
    connection, _ = server.accept()
    connection.settimeout(5)
    print("soak peer connected", flush=True)

    deadline = time.time() + args.seconds
    interval = 1.0 / max(1.0, args.rate)
    index = 0
    sent_frames = 0
    sent_junk = 0
    try:
        while time.time() < deadline:
            t = index / max(1.0, args.rate)
            values = [
                math.sin(2.0 * math.pi * t),
                math.sin(2.0 * math.pi * t + 0.7),
                math.cos(2.0 * math.pi * t),
                (index % 100) / 100.0,
                0.5 * math.sin(2.0 * math.pi * t * 3.0),
                float(index % 7),
                -0.25 * math.sin(2.0 * math.pi * t * 0.5),
                float(index % 11) * 0.1,
            ]
            connection.sendall(frame(values))
            sent_frames += 1
            index += 1

            # Corrupt stream at ~0.5%: random bytes or a half frame.
            if random.random() < 0.005:
                if random.random() < 0.5:
                    connection.sendall(os.urandom(random.randint(1, 32)))
                else:
                    partial = frame(values)
                    connection.sendall(partial[: random.randint(1, len(partial) - 1)])
                sent_junk += 1

            time.sleep(interval)
    except (BrokenPipeError, ConnectionResetError, socket.timeout):
        print("soak peer connection ended early", flush=True)
    finally:
        try:
            connection.close()
        except OSError:
            pass
        server.close()
        print(
            "soak peer done: frames={} junk={}".format(sent_frames, sent_junk),
            flush=True,
        )


if __name__ == "__main__":
    main()
