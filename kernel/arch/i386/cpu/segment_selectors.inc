# Shared selector constants for i386 assembly modules.
# Keep in sync with kernel/include/kernel/cpu/gdt.h.

.equ KERNEL_CS_SELECTOR, 0x08
.equ KERNEL_DS_SELECTOR, 0x10
.equ USER_CS_SELECTOR,   0x1B  # GDT_USER_CODE_SELECTOR | RPL3
.equ USER_DS_SELECTOR,   0x23  # GDT_USER_DATA_SELECTOR | RPL3
