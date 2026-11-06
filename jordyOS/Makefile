CC = $(if $(JORDY_CC),$(JORDY_CC),clang)
LD = ld.lld
OBJCOPY = llvm-objcopy
NASM = nasm
BPF_CC = $(if $(JORDY_CC),$(JORDY_CC),clang)
QEMU ?= qemu-system-x86_64
export QEMU
APELANG_DIR ?= ../vendor/apelang
TCPLARP_DIR ?= ../vendor/tcplarp
EBPFW_DIR ?= ../vendor/ebpfw
EBPFW_HEADERS = net/bpf/ebpfw_shared.h net/bpf/ebpfw_early.h net/bpf/ebpfw_count.h
CFLAGS = --target=x86_64-none-elf -march=x86-64 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mcmodel=small -fno-asynchronous-unwind-tables -O2 -Wall -Wextra -Wno-unused-parameter -std=c99
APEFLAGS = $(CFLAGS) -DAPE_FREESTANDING -Iape -I$(APELANG_DIR)/src
OBJS = kernel_entry.o ctx_switch.o interrupts.o kernel.o gui.o arch.o isolation.o ape/platform.o ape/seed.o net/packet.o net/firewall.o net/command.o net/network.o net/rtl8139.o net/bpf_vm.o net/fw_bpf.o net/larp.o net/tcp.o net/tcp_native.o
USER_OBJS = user/start.o user/main.o user/syscalls.o user/lib.o user/platform.o user/app_ape.o user/compiler.o user/lexer.o user/vm.o
USERFLAGS = $(APEFLAGS) -DJORDY_USER
all: os.img
kernel_entry.o: kernel_entry.asm
	$(NASM) -f elf64 $< -o $@
ctx_switch.o: ctx_switch.asm
	$(NASM) -f elf64 $< -o $@
interrupts.o: interrupts.asm
	$(NASM) -f elf64 $< -o $@
user/start.o: user/start.asm
	$(NASM) -f elf64 $< -o $@
user/%.o: user/%.c
	$(CC) $(USERFLAGS) -MMD -MP -c $< -o $@
user/platform.o: ape/platform.c
	$(CC) $(USERFLAGS) -MMD -MP -c $< -o $@
user/app_ape.o: ape/app_ape.c
	$(CC) $(USERFLAGS) -MMD -MP -c $< -o $@
user/compiler.o: $(APELANG_DIR)/src/compiler/compiler.c
	$(CC) $(USERFLAGS) -MMD -MP -c $< -o $@
user/lexer.o: $(APELANG_DIR)/src/lexer/lexer.c
	$(CC) $(USERFLAGS) -MMD -MP -c $< -o $@
user/vm.o: $(APELANG_DIR)/src/vm/vm.c
	$(CC) $(USERFLAGS) -MMD -MP -c $< -o $@
user/runtime.elf: user/linker.ld $(USER_OBJS)
	$(LD) -m elf_x86_64 -T user/linker.ld -o $@ $(USER_OBJS)
user/image.h: user/runtime.elf tools/embed_user.py
	python3 tools/embed_user.py $< $@
isolation.o: user/image.h
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
net/larp.o: net/larp.c net/larp.h $(TCPLARP_DIR)/larp_core.h
	$(CC) $(CFLAGS) -I$(TCPLARP_DIR) -MMD -MP -c $< -o $@
net/tcp.o net/tcp_native.o: net/%.o: net/%.c net/tcp.h $(TCPLARP_DIR)/larp_core.h
	$(CC) $(CFLAGS) -I$(TCPLARP_DIR) -MMD -MP -c $< -o $@
$(EBPFW_HEADERS) &: $(EBPFW_DIR)/bpf/stateful.c net/bpf/upstream.json tools/embed_ebpfw.py
	python3 tools/embed_ebpfw.py $(EBPFW_DIR) net/bpf/upstream.json net/bpf
net/bpf/program.bpf.o: net/bpf/program.bpf.c $(EBPFW_HEADERS) Makefile
	$(BPF_CC) -target bpfel -mcpu=v1 -O2 -Wall -Wextra -Werror -c $< -o $@
net/bpf/programs.h: net/bpf/program.bpf.o tools/embed_bpf.py
	python3 tools/embed_bpf.py $< $@
net/fw_bpf.o: net/bpf/programs.h
ape/platform.o: ape/platform.c
	$(CC) $(APEFLAGS) -MMD -MP -c $< -o $@
ape/programs.h: programs $(wildcard programs/*.ape) tools/embed.py
	python3 tools/embed.py
ape/seed.o: ape/seed.c ape/programs.h
	$(CC) $(APEFLAGS) -MMD -MP -c $< -o $@
kernel.elf: linker.ld $(OBJS)
	$(LD) -m elf_x86_64 -T linker.ld -o $@ $(OBJS)
kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@
bootloader.bin: bootloader.asm kernel.bin
	$(NASM) -f bin -DKERNEL_SECTORS=$$(python3 -c 'import os; print((os.path.getsize("kernel.bin")+511)//512)') $< -o $@
os.img: bootloader.bin kernel.bin tools/image.py
	python3 tools/image.py
iso: jordyos.iso
jordyos.iso: os.img tools/iso.py
	python3 tools/iso.py os.img $@
run-iso: jordyos.iso
	$(QEMU) -cpu qemu64 -drive if=ide,media=cdrom,format=raw,file=jordyos.iso -boot d -m 32 -serial stdio -snapshot -nic none
test-iso: jordyos.iso
	python3 tools/test_iso.py
	JORDY_BOOT_MEDIA=cdrom SMOKE_LOG=/tmp/jordyos-iso-smoke.log python3 tools/smoke.py
run: os.img
	$(QEMU) -cpu qemu64 -drive if=floppy,format=raw,file=os.img -boot a -m 32 -serial stdio -snapshot -nic none
run-net: os.img
	$(QEMU) -cpu qemu64 -drive if=floppy,format=raw,file=os.img -boot a -m 32 -serial stdio -snapshot -nic none -netdev user,id=lab,net=192.0.2.0/24,host=192.0.2.1,dhcpstart=192.0.2.2,ipv6=off,hostfwd=udp:127.0.0.1:17777-192.0.2.2:7777 -device rtl8139,netdev=lab,mac=52:54:00:12:34:56
HOST_CC ?= cc
HOST_CFLAGS ?= -O2
test-snake:
	$(MAKE) -C $(APELANG_DIR) CC=$(HOST_CC)
	python3 tools/test_snake.py $(APELANG_DIR)/apeslang
test-firewall: net/bpf/programs.h
	EBPFW_DIR="$(EBPFW_DIR)" python3 tools/test_ebpfw_vendor.py
	python3 tools/test_firewall.py "$(HOST_CC)" "$(HOST_CFLAGS)"
	python3 tools/test_embed_bpf.py
test-larp:
	python3 tools/test_larp.py "$(HOST_CC)" "$(HOST_CFLAGS)" "$(TCPLARP_DIR)"
test-tcp:
	python3 tools/test_tcp.py "$(HOST_CC)" "$(HOST_CFLAGS)" "$(TCPLARP_DIR)"
test-tcp-linux: os.img
	python3 tools/test_tcp_linux.py
test-network: os.img
	python3 tools/test_network.py
test-network-user: os.img
	python3 tools/test_network.py --user
test-boot: os.img
	python3 tools/test_boot.py
test-isolation: os.img
	python3 tools/test_embed_user.py
	python3 tools/test_isolation.py
test: os.img test-boot test-isolation test-snake test-firewall test-larp test-tcp test-network
	python3 tools/smoke.py
clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(USER_OBJS) $(USER_OBJS:.o=.d) *.bin *.elf os.img jordyos.iso ape/programs.h net/bpf/programs.h net/bpf/program.bpf.o $(EBPFW_HEADERS) user/runtime.elf user/image.h
-include $(OBJS:.o=.d) $(USER_OBJS:.o=.d)
.PHONY: all iso run run-iso run-net test test-iso test-boot test-isolation test-snake test-firewall test-larp test-tcp test-tcp-linux test-network test-network-user clean
$(OBJS) $(USER_OBJS): Makefile
