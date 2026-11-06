"""Register every tool test and run the full suite through its real Make targets.

Python tests need different arguments/build products, and C tests need their
host compiler drivers. An explicit registry preserves those requirements;
discovery fails on a missing or newly added unregistered test instead of
silently omitting it. Run from jordyOS via `make test-all` or `make test-inventory`.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

SUITES = {
    "lint-workflow": [],
    "test-boot": ["test_boot.py"],
    "test-isolation": ["test_embed_user.py", "test_isolation.py"],
    "test-snake": ["test_snake.py"],
    "test-firewall": ["test_ebpfw_vendor.py", "test_firewall.py", "test_firewall.c",
                      "test_bpf.c", "test_embed_bpf.py"],
    "test-larp": ["test_larp.py"],
    "test-tcp": ["test_tcp.py", "test_tcp.c"],
    "test-network": ["test_network.py"],
    "test-smoke": ["smoke.py"],
    "test-network-user": ["test_network.py"],
    "test-iso": ["test_iso.py", "smoke.py"],
    "test-tcp-linux": ["test_tcp_linux.py"],
}


def inventory():
    tools = Path(__file__).resolve().parent
    discovered = {p.name for p in tools.glob("test_*.*") if p.suffix in (".py", ".c")}
    if (tools / "smoke.py").is_file():
        discovered.add("smoke.py")
    registered = {name for files in SUITES.values() for name in files}
    unregistered = discovered - registered
    missing = registered - discovered
    if unregistered or missing:
        raise SystemExit(f"Test inventory mismatch: unregistered={sorted(unregistered)}, "
                         f"missing={sorted(missing)}. Update tools/run_tests.py.")
    for target, files in SUITES.items():
        print(f"{target}: {', '.join(files) or 'GitHub Actions semantic validation'}", flush=True)
    print(f"PASS: all {len(discovered)} test files registered in {len(SUITES)} suites", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="Validate and print test inventory only")
    parser.add_argument("--make", default="make")
    args = parser.parse_args()
    inventory()
    if args.list:
        return
    fixture = os.environ.get("TCP_LINUX_DIR")
    if not fixture or not all((Path(fixture) / name).is_file()
                             for name in ("vmlinuz", "initramfs.cpio.gz")):
        raise SystemExit("Full suite requires the Linux fixture: run tools/prepare_tcp_linux.py "
                         "and set TCP_LINUX_DIR. See CI.md; no suites have been skipped.")
    subprocess.run([*shlex.split(args.make), *SUITES], check=True)
    print("ALL JORDYOS TEST SUITES PASSED", flush=True)


if __name__ == "__main__":
    main()
