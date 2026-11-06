# JordyOS test tools

Run the complete suite through the same entry point used by CI:

```sh
# From the repository root, using the pinned Nix environment:
nix develop --command python3 jordyOS/tools/prepare_tcp_linux.py \
  --cache /tmp/jordyos-linux-assets --output /tmp/jordyos-linux
TCP_LINUX_DIR=/tmp/jordyos-linux nix develop --command make -C jordyOS test-all
```

Every command must succeed for the suite to print
`ALL JORDYOS TEST SUITES PASSED`. `run_tests.py` registers the actual test
files and invokes their Make targets with the required builds/arguments.
Adding a `test_*.py` or `test_*.c` file without registering it fails the
inventory check. Use `make -C jordyOS test-inventory` to inspect that mapping.

| Make target | Test files | What it exercises |
| --- | --- | --- |
| `lint-workflow` | GitHub workflow via actionlint | Workflow syntax, expressions, context availability and shell commands |
| `test-boot` | `test_boot.py` | Real QEMU long-mode registers/paging and unsupported-CPU rejection |
| `test-isolation` | `test_embed_user.py`, `test_isolation.py` | ELF image validation, real ring-3 access faults/private pages and recovery |
| `test-snake` | `test_snake.py` | Actual Apelang Snake rules via the hosted VM |
| `test-firewall` | `test_ebpfw_vendor.py`, `test_firewall.py`, `test_firewall.c`, `test_bpf.c`, `test_embed_bpf.py` | Upstream source/hash checks, decoding/policy/eBPF VM, real compiled programs and ELF validation |
| `test-larp` | `test_larp.py` | Actual upstream LARP controller with deterministic feedback |
| `test-tcp` | `test_tcp.py`, `test_tcp.c` | Sequence wrap, ACK/window/FIN handling, loss recovery, pacing, file bounds and deadlines |
| `test-network` | `test_network.py` | RTL8139 DMA and real Ethernet traffic, eBPF drops and malformed packets |
| `test-smoke` | `smoke.py` | Actual guest shell, compiler/REPL/bytecode, apps, RAM files and desktop |
| `test-network-user` | `test_network.py --user` | Host UDP forwarding through QEMU user networking |
| `test-iso` | `test_iso.py`, `smoke.py` with CD boot | ISO/catalog checks and the complete app suite booted from CD |
| `test-tcp-linux` | `test_tcp_linux.py` | Real Linux/JordyOS binary and file transfers, hashes, delay/loss, firewall and cancellation |

The ordinary `make test` remains the smaller core suite. `test-all` includes
every row, including the Linux fixture requirement. See [../CI.md](../CI.md)
for transcripts, artifacts, prerequisites and CI behavior.

The remaining Python files supply build or lab functions: `embed*.py`,
`image.py`, and `iso.py` produce build artifacts; `prepare_tcp_linux.py`
prepares the checksum-pinned Linux guest; `tcp_echo.py` is the interactive
loopback echo server. Their validation is covered by the corresponding
image, embedding, networking and interoperability tests above.
