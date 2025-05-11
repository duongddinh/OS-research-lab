ISO = calc.iso
CC = i686-elf-gcc
LD = i686-elf-ld
CFLAGS = -ffreestanding -O2 -Wall -Wextra -std=c99
OBJS = kernel_entry.o kernel.o

all: $(ISO)

KSECT := $(shell echo $$(( ( $(shell stat -f%z kernel.bin) + 511 ) / 512 )) )

bootloader.bin: bootloader.asm
	nasm -f bin -DKERNEL_SECTORS=$(KSECT) $< -o $@

kernel_entry.o: kernel_entry.asm
	nasm -f elf32 $< -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel.bin: linker.ld $(OBJS)
	$(LD) -T linker.ld -o kernel.elf $(OBJS)
	i686-elf-objcopy -O binary kernel.elf $@

$(ISO): bootloader.bin kernel.bin
	dd if=/dev/zero of=os.img bs=512 count=2880
	dd if=bootloader.bin of=os.img conv=notrunc
	dd if=kernel.bin of=os.img bs=512 seek=1 conv=notrunc
	mkisofs -V 'CALCOS' -input-charset iso8859-1 -o $(ISO) -b os.img .

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO)
	
run-floppy: os.img
	qemu-system-i386 -drive if=floppy,format=raw,file=os.img -boot a -m 32

clean:
	rm -f *.o *.bin *.elf os.img $(ISO)
