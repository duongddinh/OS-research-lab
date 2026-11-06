# Automated JordyOS tests

`.github/workflows/jordyos-tests.yml` runs on pull requests and manual
dispatch. It does not deploy or mutate infrastructure. Tests are not repeated
automatically just because a PR was merged. Manual dispatch becomes available
in the Actions UI after the workflow exists on the default branch.

The GitHub-hosted Ubuntu 24.04 runner checks out all three pinned submodules:
`vendor/apelang`, `vendor/tcplarp`, and `vendor/ebpfw`. Nix uses `flake.lock`
for the compiler, assembler, linker, QEMU, Python and Make toolchain. GitHub
Actions themselves are pinned by commit ID, permissions are read-only, and
no project secrets or external host access are needed. The job times out
after 30 minutes; superseded runs are cancelled. Hosted runners are ephemeral,
and uploaded artifacts expire after seven days.

The workflow prepares the Linux fixture and runs the complete entry point:

```sh
git submodule update --init --recursive
nix develop --command python3 jordyOS/tools/prepare_tcp_linux.py \
  --cache /tmp/jordyos-linux-assets --output /tmp/jordyos-linux
TCP_LINUX_DIR=/tmp/jordyos-linux nix develop --command make -C jordyOS test-all
```

`test` covers boot/CPU rejection, ring-3 isolation and private mappings,
Apelang/compiler/bytecode/REPL/apps, Snake rules, vendored-ebpfw extraction,
eBPF VM/policy and ELF validation, LARP controller scenarios, deterministic
TCP cases, and actual RTL8139 packet traffic. `test-network-user` checks host
UDP forwarding. `test-iso` validates El Torito and runs the app smoke suite
from a CD boot with no floppy attached.

Linux interoperability prepares SHA256-verified Alpine/Linux assets, then
runs two diskless QEMU guests connected only by local sockets. It tests binary
TCP echo, file upload/download/exchange, Linux SHA256 checks, transferred
Apelang execution, delay, dropped handshake/data/close packets, refusal,
firewall enforcement and cancellation. The guests have no host mounts,
credentials, host interfaces, or external networking. Fixture download occurs
in a separate preparation step; the traffic test itself is offline.

## One complete test entry point

`make -C jordyOS test-all` is the comprehensive entry point used by CI. It
runs semantic workflow validation and every suite registered in
`tools/run_tests.py`, including the otherwise separate host-forwarding,
CD-boot and Linux interoperability suites. It fails if any `test_*.py`,
`test_*.c`, or `smoke.py` file is missing from that registry, or if the
prepared Linux fixture is absent. It does not silently skip those checks.

Run `nix develop --command make -C jordyOS lint-workflow` before pushing a
workflow change. `make -C jordyOS test-inventory` prints the complete
file-to-suite mapping. `actionlint` is included in the locked Nix environment
and checks GitHub expression/context availability as well as YAML structure.
In particular, `runner.temp` is allowed in step-level `env`, but not job-level
`env`; the workflow uses the step-level context for the Linux suite.

After success, download **jordyos-boot-images** from the workflow run's
Artifacts section. It contains `jordyos.iso` and `os.img`. **jordyos-test-transcripts**
contains serial logs and is uploaded even on failure when logs exist. A
passing QEMU ISO test does not certify VirtualBox or VMware compatibility;
see [VIRTUAL_MACHINES.md](VIRTUAL_MACHINES.md).
