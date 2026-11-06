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

The workflow runs:

```sh
git submodule update --init --recursive
nix develop --command make -C jordyOS test test-network-user test-iso
nix develop --command python3 jordyOS/tools/prepare_tcp_linux.py \
  --cache /tmp/jordyos-linux-assets --output /tmp/jordyos-linux
TCP_LINUX_DIR=/tmp/jordyos-linux nix develop --command make -C jordyOS test-tcp-linux
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

After success, download **jordyos-boot-images** from the workflow run's
Artifacts section. It contains `jordyos.iso` and `os.img`. **jordyos-test-transcripts**
contains serial logs and is uploaded even on failure when logs exist. A
passing QEMU ISO test does not certify VirtualBox or VMware compatibility;
see [VIRTUAL_MACHINES.md](VIRTUAL_MACHINES.md).
