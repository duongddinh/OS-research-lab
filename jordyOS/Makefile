CC = $(if $(JORDY_CC),$(JORDY_CC),clang)
LD = ld.lld
OBJCOPY = llvm-objcopy
NASM = nasm
APELANG_DIR ?= ../vendor/apelang
CFLAGS = --target=i686-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mno-sse -mno-sse2 -mstack-alignment=4 -O2 -Wall -Wextra -Wno-unused-parameter -std=c99
APEFLAGS = $(CFLAGS) -DAPE_FREESTANDING -Iape -I$(APELANG_DIR)/src
OBJS = kernel_entry.o ctx_switch.o kernel.o gui.o ape/platform.o ape/app_ape.o ape/compiler.o ape/lexer.o ape/vm.o
all: os.img
kernel_entry.o: kernel_entry.asm
	$(NASM) -f elf32 $< -o $@
ctx_switch.o: ctx_switch.asm
	$(NASM) -f elf32 $< -o $@
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
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
HOST_CC ?= cc
test-snake:
	$(MAKE) -C $(APELANG_DIR) CC=$(HOST_CC)
	python3 tools/test_snake.py $(APELANG_DIR)/apeslang
test: os.img test-snake
	python3 tools/smoke.py
clean:
	rm -f $(OBJS) $(OBJS:.o=.d) *.bin *.elf os.img ape/programs.h
-include $(OBJS:.o=.d)
.PHONY: all run test test-snake clean
