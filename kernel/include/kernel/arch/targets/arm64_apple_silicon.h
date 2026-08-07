/**
 * @file arm64_apple_silicon.h
 * @brief Target declaration: arm64 on apple_silicon.
 *
 * SCAFFOLD — declared, not implemented, and the most expensive entry on this list.
 *
 * The ISA is shared with the Raspberry Pi; the platform is proprietary, largely
 * undocumented, boots through iBoot rather than any standard firmware, and uses a
 * 16 KiB page granule where everything else here uses 4 KiB. Asahi Linux spent
 * years on this platform, and none of that cost sits on the ISA axis — which is
 * precisely what the two-axis split is meant to make visible before starting.
 */

#ifndef KERNEL_ARCH_TARGETS_ARM64_APPLE_SILICON_H
#define KERNEL_ARCH_TARGETS_ARM64_APPLE_SILICON_H

#define KERNEL_ARCH_NAME          "arm64"
#define KERNEL_ARCH_PLATFORM_NAME "apple_silicon"

#define KERNEL_ARCH_HAS_MEMORY_MANAGEMENT_UNIT            1
#define KERNEL_ARCH_HAS_PAGING                            1
#define KERNEL_ARCH_HAS_HIGHER_HALF                       1
#define KERNEL_ARCH_HAS_WRITE_PROTECT_ENFORCEMENT         1
#define KERNEL_ARCH_HAS_SYMMETRIC_MULTIPROCESSING         1
#define KERNEL_ARCH_HAS_FLOATING_POINT_UNIT               1
#define KERNEL_ARCH_HAS_SINGLE_INSTRUCTION_MULTIPLE_DATA  1
#define KERNEL_ARCH_HAS_CACHE_COHERENT_DIRECT_MEMORY_ACCESS 1
#define KERNEL_ARCH_HAS_PORT_INPUT_OUTPUT                 0
#define KERNEL_ARCH_HAS_PERIPHERAL_COMPONENT_INTERCONNECT 0
#define KERNEL_ARCH_HAS_FIRMWARE_TABLES                   0
#define KERNEL_ARCH_HAS_DEVICE_TREE                       1

#define KERNEL_ARCH_POINTER_WIDTH_BITS          64
#define KERNEL_ARCH_PHYSICAL_ADDRESS_WIDTH_BITS 48
#define KERNEL_ARCH_PAGE_SIZE_BYTES             16384

#endif /* KERNEL_ARCH_TARGETS_ARM64_APPLE_SILICON_H */
