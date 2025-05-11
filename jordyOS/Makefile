ISO = calc.iso
CC  = i686-elf-gcc
LD  = i686-elf-ld
CFLAGS = -ffreestanding -O2 -Wall -Wextra -std=c99
OBJS = kernel_entry.o ctx_switch.o kernel.o app_calc.o app_edit.o

KSECT = $(shell echo $$(( ( $(shell stat -f%z kernel.bin) + 511 ) / 512 )) )

all: os.img

bootloader.bin: bootloader.asm kernel.bin          
	@ksect=$$(echo $$(( ( $(shell stat -f%z kernel.bin) + 511 ) / 512 ))); \
	 echo "  [LD]  writing bootloader for $$ksect sectors"; \
	 nasm -f bin -DKERNEL_SECTORS=$$ksect $< -o $@

kernel_entry.o: kernel_entry.asm
	nasm -f elf32 $< -o $@

ctx_switch.o: ctx_switch.asm
	nasm -f elf32 $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel.bin: linker.ld $(OBJS)
	$(LD) -T linker.ld -o kernel.elf $(OBJS)
	i686-elf-objcopy -O binary kernel.elf $@

os.img: bootloader.bin kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=2880
	dd if=bootloader.bin of=$@ conv=notrunc
	dd if=kernel.bin   of=$@ bs=512 seek=1 conv=notrunc

run: os.img
	qemu-system-i386 -drive if=floppy,format=raw,file=os.img -boot a -m 32

clean:
	rm -f *.o *.bin *.elf os.img $(ISO)
