"""Host tests inject deterministic sequence numbers, clocks and NIC backpressure."""
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="jordyos-tcp-") as directory:
    binary = str(Path(directory) / "test")
    subprocess.run([*shlex.split(sys.argv[1]), *shlex.split(sys.argv[2]),
        "-std=c99", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-I.",
        "-I" + sys.argv[3], "net/tcp.c", "net/packet.c", "tools/test_tcp.c",
        "-o", binary], check=True)
    subprocess.run([binary], check=True, timeout=30)
