/**
 * @file riscv64_virt.h
 * @brief Target declaration: riscv64 on virt.
 *
 * SCAFFOLD — declared, not implemented.
 *
 * The cheapest non-x86 port to attempt first, and the one worth doing before any
 * hardware: QEMU's `virt` machine is documented, stable, and describes everything
 * it has in a device tree. Sv39 paging is three levels where x86 is two. The vector
 * extension is not assumed present, so no SIMD is declared — a target claims what
 * it is guaranteed to have, never what it might.
 */

#ifndef KERNEL_ARCH_TARGETS_RISCV64_VIRT_H
#define KERNEL_ARCH_TARGETS_RISCV64_VIRT_H

#define KERNEL_ARCH_NAME          "riscv64"
#define KERNEL_ARCH_PLATFORM_NAME "virt"

#define KERNEL_ARCH_HAS_MEMORY_MANAGEMENT_UNIT              1
#define KERNEL_ARCH_HAS_PAGING                              1
#define KERNEL_ARCH_HAS_HIGHER_HALF                         1
#define KERNEL_ARCH_HAS_WRITE_PROTECT_ENFORCEMENT           1
#define KERNEL_ARCH_HAS_SYMMETRIC_MULTIPROCESSING           1
#define KERNEL_ARCH_HAS_FLOATING_POINT_UNIT                 1
#define KERNEL_ARCH_HAS_SINGLE_INSTRUCTION_MULTIPLE_DATA    0
#define KERNEL_ARCH_HAS_CACHE_COHERENT_DIRECT_MEMORY_ACCESS 1
#define KERNEL_ARCH_HAS_PORT_INPUT_OUTPUT                   0
#define KERNEL_ARCH_HAS_PERIPHERAL_COMPONENT_INTERCONNECT   0
#define KERNEL_ARCH_HAS_FIRMWARE_TABLES                     0
#define KERNEL_ARCH_HAS_DEVICE_TREE                         1

#define KERNEL_ARCH_POINTER_WIDTH_BITS          64
#define KERNEL_ARCH_PHYSICAL_ADDRESS_WIDTH_BITS 56
#define KERNEL_ARCH_PAGE_SIZE_BYTES             4096

#endif /* KERNEL_ARCH_TARGETS_RISCV64_VIRT_H */
