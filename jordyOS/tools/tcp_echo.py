"""Loopback-only TCP echo peer for `make run-net` and `tcp demo 192.0.2.1 9000`."""
import argparse
import hashlib
import socket

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", type=int, default=9000)
args = parser.parse_args()
with socket.socket() as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", args.port))
    server.listen(1)
    print(f"TCP echo listening on 127.0.0.1:{args.port}", flush=True)
    while True:
        connection, _ = server.accept()
        with connection:
            connection.settimeout(125)
            data = bytearray()
            while len(data) <= 16384:
                chunk = connection.recv(min(4096, 16385-len(data)))
                if not chunk:
                    break
                data.extend(chunk)
            if len(data) > 16384:
                print("Rejected transfer larger than 16 KiB", flush=True)
                continue
            connection.sendall(data)
            print(f"Echoed {len(data)} bytes; SHA256={hashlib.sha256(data).hexdigest()}", flush=True)
