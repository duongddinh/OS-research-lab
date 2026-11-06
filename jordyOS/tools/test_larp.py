"""Run the same upstream-controller scenarios on the host and in QEMU's smoke test."""
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="jordyos-larp-") as directory:
    main = Path(directory) / "main.c"
    main.write_text('''#include <stdio.h>
#include "net/larp.h"
int main(void) {
    const char *failure = larp_selftest();
    if (failure) { fprintf(stderr, "LARP FAIL: %s\\n", failure); return 1; }
    puts("PASS upstream LARP core: launch, feedback, loss/ECN, RTT, recovery, arithmetic");
    return 0;
}
''')
    binary = str(Path(directory) / "test")
    subprocess.run([*shlex.split(sys.argv[1]), *shlex.split(sys.argv[2]),
        "-std=c99", "-Wall", "-Wextra", "-Werror", "-I.",
        "-I" + sys.argv[3], "net/larp.c", str(main), "-o", binary], check=True)
    subprocess.run([binary], check=True, timeout=30)
