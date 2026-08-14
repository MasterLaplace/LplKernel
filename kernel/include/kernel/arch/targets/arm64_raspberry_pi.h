/**
 * @file arm64_raspberry_pi.h
 * @brief Target declaration: arm64 on raspberry_pi.
 *
 * SCAFFOLD — declared, not implemented.
 *
 * Same ISA as Apple silicon and almost nothing else in common, which is the whole
 * argument for keeping ISA and platform on separate axes. No port I/O at all (every
 * device is memory-mapped), no PCI on the older boards, no ACPI: hardware is
 * enumerated from a device tree the firmware hands over.
 *
 * DMA is deliberately NOT declared cache coherent. On BCM parts a driver must clean
 * and invalidate by hand, and that is a correctness requirement rather than a
 * performance note — a driver written against a coherent platform corrupts data
 * here, silently and intermittently.
 */

#ifndef KERNEL_ARCH_TARGETS_ARM64_RASPBERRY_PI_H
#define KERNEL_ARCH_TARGETS_ARM64_RASPBERRY_PI_H

#define KERNEL_ARCH_NAME          "arm64"
#define KERNEL_ARCH_PLATFORM_NAME "raspberry_pi"

#define KERNEL_ARCH_HAS_MEMORY_MANAGEMENT_UNIT              1
#define KERNEL_ARCH_HAS_PAGING                              1
#define KERNEL_ARCH_HAS_HIGHER_HALF                         1
#define KERNEL_ARCH_HAS_WRITE_PROTECT_ENFORCEMENT           1
#define KERNEL_ARCH_HAS_SYMMETRIC_MULTIPROCESSING           1
#define KERNEL_ARCH_HAS_FLOATING_POINT_UNIT                 1
#define KERNEL_ARCH_HAS_SINGLE_INSTRUCTION_MULTIPLE_DATA    1
#define KERNEL_ARCH_HAS_CACHE_COHERENT_DIRECT_MEMORY_ACCESS 0
#define KERNEL_ARCH_HAS_PORT_INPUT_OUTPUT                   0
#define KERNEL_ARCH_HAS_PERIPHERAL_COMPONENT_INTERCONNECT   0
#define KERNEL_ARCH_HAS_FIRMWARE_TABLES                     0
#define KERNEL_ARCH_HAS_DEVICE_TREE                         1

#define KERNEL_ARCH_POINTER_WIDTH_BITS          64
#define KERNEL_ARCH_PHYSICAL_ADDRESS_WIDTH_BITS 48
#define KERNEL_ARCH_PAGE_SIZE_BYTES             4096

#endif /* KERNEL_ARCH_TARGETS_ARM64_RASPBERRY_PI_H */
