# Shorthand	Meaning
# $(CC)	C Compiler (cross compiler version we just setup)
# $(CXX)	C++ compiler (cross-compiler version)
# $(LD)	Linker (again, cross compiler version)
# $(C_SRCS)	All the C source files to compile
# $(OBJS)	All the object files to link

CC=clang --target=x86_64-elf
CXX=clang++ --target=x86_64-elf
LD=ld.lld
C_SRCS=$(wildcard src/*.c)
OBJS=$(C_SRCS:.c=.o)
NAME=iso/boot/hello_world.elf
LD_SCRIPT = linker.ld
ISO_NAME = my_os.iso

all: $(C_SRCS) $(OBJS)
	$(LD) $(OBJS) -o $(NAME) -T $(LD_SCRIPT) -nostdlib -static -pie --no-dynamic-linker

%.o: %.c
	$(CC) -c $< -o $@ -ffreestanding

%.o: %.cpp
	$(CXX) -c $< -o $@ -ffreestanding -fno-rtti -fno-exceptions

iso: all
	grub-mkrescue -o $(ISO_NAME) iso

run:
	qemu-system-x86_64 -cdrom $(ISO_NAME)

run-with-kvm:
	qemu-system-x86_64 -cdrom $(ISO_NAME) --enable-kvm

clean:
	rm -f $(OBJS) $(NAME) $(ISO_NAME)
