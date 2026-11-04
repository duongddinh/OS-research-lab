CC = $(if $(JORDY_CC),$(JORDY_CC),clang)
LD = ld.lld
OBJCOPY = llvm-objcopy
NASM = nasm
BPF_CC = $(if $(JORDY_CC),$(JORDY_CC),clang)
APELANG_DIR ?= ../vendor/apelang
CFLAGS = --target=i686-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mno-sse -mno-sse2 -mstack-alignment=4 -O2 -Wall -Wextra -Wno-unused-parameter -std=c99
APEFLAGS = $(CFLAGS) -DAPE_FREESTANDING -Iape -I$(APELANG_DIR)/src
OBJS = kernel_entry.o ctx_switch.o kernel.o gui.o ape/platform.o ape/app_ape.o ape/compiler.o ape/lexer.o ape/vm.o net/packet.o net/firewall.o net/command.o net/network.o net/rtl8139.o net/bpf_vm.o net/fw_bpf.o
all: os.img
kernel_entry.o: kernel_entry.asm
	$(NASM) -f elf32 $< -o $@
ctx_switch.o: ctx_switch.asm
	$(NASM) -f elf32 $< -o $@
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
net/bpf/program.bpf.o: net/bpf/program.bpf.c net/bpf/ebpfw_shared.h net/bpf/ebpfw_early.h
	$(BPF_CC) -target bpfel -mcpu=v1 -O2 -Wall -Wextra -Werror -c $< -o $@
net/bpf/programs.h: net/bpf/program.bpf.o tools/embed_bpf.py
	python3 tools/embed_bpf.py $< $@
net/fw_bpf.o: net/bpf/programs.h
ape/platform.o: ape/platform.c
	$(CC) $(APEFLAGS) -MMD -MP -c $< -o $@
ape/programs.h: programs $(wildcard programs/*.ape) tools/embed.py
	python3 tools/embed.py
ape/app_ape.o: ape/app_ape.c ape/programs.h
	$(CC) $(APEFLAGS) -MMD -MP -c $< -o $@
ape/compiler.o: $(APELANG_DIR)/src/compiler/compiler.c
	$(CC) $(APEFLAGS) -MMD -MP -c $< -o $@
ape/lexer.o: $(APELANG_DIR)/src/lexer/lexer.c
	$(CC) $(APEFLAGS) -MMD -MP -c $< -o $@
ape/vm.o: $(APELANG_DIR)/src/vm/vm.c
	$(CC) $(APEFLAGS) -MMD -MP -c $< -o $@
kernel.elf: linker.ld $(OBJS)
	$(LD) -m elf_i386 -T linker.ld -o $@ $(OBJS)
kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@
bootloader.bin: bootloader.asm kernel.bin
	$(NASM) -f bin -DKERNEL_SECTORS=$$(python3 -c 'import os; print((os.path.getsize("kernel.bin")+511)//512)') $< -o $@
os.img: bootloader.bin kernel.bin
	python3 tools/image.py
run: os.img
	qemu-system-i386 -drive if=floppy,format=raw,file=os.img -boot a -m 32 -serial stdio -snapshot -nic none
run-net: os.img
	qemu-system-i386 -drive if=floppy,format=raw,file=os.img -boot a -m 32 -serial stdio -snapshot -nic none -netdev user,id=lab,net=192.0.2.0/24,host=192.0.2.1,dhcpstart=192.0.2.2,ipv6=off,hostfwd=udp:127.0.0.1:17777-192.0.2.2:7777 -device rtl8139,netdev=lab,mac=52:54:00:12:34:56
HOST_CC ?= cc
HOST_CFLAGS ?= -O2
test-snake:
	$(MAKE) -C $(APELANG_DIR) CC=$(HOST_CC)
	python3 tools/test_snake.py $(APELANG_DIR)/apeslang
test-firewall: net/bpf/programs.h
	python3 tools/test_firewall.py "$(HOST_CC)" "$(HOST_CFLAGS)"
	python3 tools/test_embed_bpf.py
test-network: os.img
	python3 tools/test_network.py
test-network-user: os.img
	python3 tools/test_network.py --user
test: os.img test-snake test-firewall test-network
	python3 tools/smoke.py
clean:
	rm -f $(OBJS) $(OBJS:.o=.d) *.bin *.elf os.img ape/programs.h net/bpf/programs.h net/bpf/program.bpf.o
-include $(OBJS:.o=.d)
.PHONY: all run run-net test test-snake test-firewall test-network test-network-user clean
