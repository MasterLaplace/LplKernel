
all:
	nasm -f elf32 boot.asm -o boot.o
	gcc -m32 -c kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
	ld -m elf_i386 -T linker.ld -o mykernel.bin boot.o kernel.o
	mkdir -p isodir/boot/grub
	cp mykernel.bin isodir/boot/mykernel.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso isodir
	qemu-system-x86_64 -cdrom myos.iso

clean:
	rm -rf *.bin *.dis *.o myos.iso isodir