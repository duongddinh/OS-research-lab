"""Compile native tests into a unique disposable directory; no shared binary."""
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="jordyos-firewall-") as directory:
    binary = str(Path(directory) / "test")
    subprocess.run([*shlex.split(sys.argv[1]), *shlex.split(sys.argv[2]), "-std=c99",
        "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-I.", "net/packet.c", "net/firewall.c",
        "tools/test_firewall.c", "-o", binary], check=True)
    subprocess.run([binary], check=True, timeout=30)
