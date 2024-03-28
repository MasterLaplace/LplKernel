##
## LAPLACE PROJECT, 2024
## Laplace [LplKernel]
## Author: MasterLaplace
## Created: 2024-03-28
## File description:
## Makefile
##


CC	=	clang --target=x86_64-elf
CXX	=	clang++ --target=x86_64-elf
LD	=	ld.lld
C_SRCS	=	$(shell find ./src -type f -name "*.c")
CXX_SRCS	=	$(shell find ./src -type f -name "*.cpp")
OBJS	=	$(C_SRCS:.c=.o) $(CXX_SRCS:.cpp=.o)
NAME	=	iso/boot/kernel.elf
ISO_FILENAME	=	kernel.iso

CPU_FEATURES	=	-mno-red-zone \
					-mno-80387 \
					-mno-mmx \
					-mno-3dnow \
					-mno-sse \
					-mno-sse2 \
					-mcmodel=kernel \
					-fno-stack-protector \
					-fno-omit-frame-pointer \
					-Wall \
					-Wextra

CXX_FLAGS	=	-fno-rtti \
				-fno-exceptions \
				-fno-unwind-tables \
				-fno-asynchronous-unwind-tables

DEPENDENCIES	=	nasm \
					grub-pc-bin \
					xorriso \
					qemu-system-x86 \
					lld

DEBUG	=	-DDEBUG \
			-g \
			-O0 \
			-fsanitize=address \
			-fsanitize=undefined \
			-fno-omit-frame-pointer

QEMU_DEBUG	=	-s \
				-S \
				-monitor stdio \
				-no-reboot \
				-no-shutdown

all: pre_install kernel clean run

re: fclean kernel clean run

pre_install:
	@echo "Installing required packages... (debian/ubuntu)"
	@sudo apt-get update
	@sudo apt-get install -y $(DEPENDENCIES)

kernel: $(OBJS)
	@echo "Linking kernel..."
	@$(LD) $(OBJS) -o $(NAME) -nostdlib -static -pie --no-dynamic-linker
	@grub-mkrescue -o $(ISO_FILENAME) iso

debug: CPU_FEATURES += $(DEBUG)
debug: kernel

run:
	@qemu-system-x86_64 -cdrom $(ISO_FILENAME)

run_debug:
	@qemu-system-x86_64 -cdrom $(ISO_FILENAME) $(QEMU_DEBUG)

run_kvm:
	@echo "Running with KVM... (need sudo)"
	@qemu-system-x86_64 -cdrom $(ISO_FILENAME) -enable-kvm

clean:
	@echo "Cleaning object files..."
	@rm -f $(OBJS)

fclean: clean
	@echo "Cleaning binary..."
	@rm -f $(NAME)
	@rm -f $(ISO_FILENAME)

view:
	@ls -lla $(NAME)
	@objdump -d $(NAME)

%.o: %.c
	@echo "Compiling $<..."
	@$(CC) $< -c -o $@ -ffreestanding $(CPU_FEATURES)

%.o: %.cpp
	@echo "Compiling $<..."
	@$(CXX) $< -c -o $@ -ffreestanding $(CPU_FEATURES) $(CXX_FLAGS)
