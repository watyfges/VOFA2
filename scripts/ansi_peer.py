#!/usr/bin/env python3
"""Send ANSI-coloured log lines to VOFA2 for terminal testing."""
import socket
import sys
import time

port = int(sys.argv[1]) if len(sys.argv) > 1 else 15420
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("127.0.0.1", port))
server.listen(1)
print("listening on", port, flush=True)
conn, addr = server.accept()
print("accepted", addr, flush=True)
end = time.time() + seconds
i = 0
try:
    while time.time() < end:
        conn.sendall(b"\x1b[31mERR red line %d\x1b[0m normal\r\n" % i)
        conn.sendall(b"\x1b[32mOK green line %d\x1b[0m\r\n" % i)
        i += 1
        time.sleep(0.2)
finally:
    conn.close()
    server.close()
