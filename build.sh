clear
rm -rf bin/*.o bin/*.iso bin/*.bin bin/*.elf 2>/dev/null
mkdir -p bin
i686-gnu-as src/boot.s -o bin/boot.o
clang --target=i686-elf-gcc -c src/kernel.c -o bin/kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
clang --target=i686-elf-gcc -T linker.ld -o bin/laplace.elf -ffreestanding -O2 -nostdlib bin/*.o -lgcc
cp bin/laplace.elf iso/boot/laplace.elf
qemu-system-i386 -kernel bin/laplace.elf
