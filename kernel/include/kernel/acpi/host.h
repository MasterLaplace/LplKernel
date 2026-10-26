/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file host.h
 * @brief What the ACPI layer asks of the machine it runs on.
 *
 * The ACPI tables and their readers hold no architecture: they reach the machine through a host
 * alone, a value they are given. kernel/arch/<arch>/cpu/acpi_host.c gives the machine's, in ring 0;
 * kernel/tests/acpi/simulated_host.c gives one that serves made-up and recorded firmware.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_ACPI_HOST_H
#define KERNEL_ACPI_HOST_H

#include <stdint.h>

/**
 * @brief What the memory at a physical address is, as the boot memory map says (§5.3).
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMemoryType {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_UNKNOWN = 0,      /**< No entry of the map covers it whole. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_RAM = 1,          /**< Usable memory. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_ACPI_RECLAIM = 2, /**< ACPI tables, reclaimable. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_ACPI_NVS = 3,     /**< Firmware's non-volatile storage. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_RESERVED = 4,     /**< Reserved: devices, or memory not ours. */
} AdvancedConfigurationAndPowerInterfaceMemoryType_t;

/**
 * @brief What the ACPI layer asks of the machine it runs on.
 *
 * @details Each call is given back the context first. map_firmware maps the length bytes the
 *          firmware wrote at a physical address, cached and read-only, and returns their address,
 *          or NULL when the range cannot be mapped; unmap_firmware takes back what it returned,
 *          given the same length. map_firmware_writable maps memory cached and writable, map_device
 *          maps it uncached, and each has its own unmap. read_memory and write_memory make one
 *          access of 1, 2 or 4 bytes to what one of those two mapped, given both the address it
 *          returned and the physical address beneath; compare_exchange swaps a 4-byte word there
 *          atomically when it holds the expected value, and returns what it held. port_read and
 *          port_write make one access of 1, 2 or 4 bytes to an I/O port. pci_read and pci_write make
 *          one access of 1, 2 or 4 bytes, at an offset that width divides, to the configuration space
 *          of a PCI function; an absent function reads all ones. sleep_milliseconds waits at
 *          least that many milliseconds and returns how many it waited, as its clock counts them,
 *          which may be more; stall_microseconds busy-waits at least that many microseconds and
 *          returns how many it waited, as its clock counts them;
 *          clock_100ns reads a monotonic clock in units of 100 ns, which the waits advance.
 *          allocate_pages gives count pages of 4 KiB, contiguous and writable, or NULL; release_pages
 *          takes them back, given the same count.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceHost {
    void *context;                                              /**< The host's own state, or NULL. */
    uint64_t (*root_system_description_pointer)(void *context); /**< The root pointer's physical address, or 0. */
    const void *(*map_firmware)(void *context, uint64_t physical_address, uint64_t length); /**< Maps firmware bytes. */
    void (*unmap_firmware)(void *context, const void *address, uint64_t length);            /**< Unmaps them. */
    uint8_t (*memory_type)(void *context, uint64_t physical_address,
                           uint64_t length); /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_* type. */
    void *(*map_firmware_writable)(void *context, uint64_t physical_address,
                                   uint64_t length);                                /**< Maps memory cached. */
    void (*unmap_firmware_writable)(void *context, void *address, uint64_t length); /**< Unmaps it. */
    void *(*map_device)(void *context, uint64_t physical_address, uint64_t length); /**< Maps memory uncached. */
    void (*unmap_device)(void *context, void *address, uint64_t length);            /**< Unmaps it. */
    uint32_t (*read_memory)(void *context, const void *address, uint64_t physical_address,
                            uint8_t width); /**< Reads mapped memory. */
    void (*write_memory)(void *context, void *address, uint64_t physical_address, uint8_t width,
                         uint32_t value); /**< Writes it. */
    uint32_t (*compare_exchange)(void *context, void *address, uint64_t physical_address, uint32_t expected,
                                 uint32_t desired);                     /**< Swaps a word of it atomically. */
    uint32_t (*port_read)(void *context, uint16_t port, uint8_t width); /**< Reads an I/O port. */
    void (*port_write)(void *context, uint16_t port, uint8_t width, uint32_t value); /**< Writes one. */
    uint32_t (*pci_read)(void *context, uint16_t segment, uint8_t bus, uint8_t device, uint8_t function,
                         uint16_t offset, uint8_t width); /**< Reads PCI configuration space. */
    void (*pci_write)(void *context, uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset,
                      uint8_t width, uint32_t value);                     /**< Writes it. */
    uint32_t (*sleep_milliseconds)(void *context, uint32_t milliseconds); /**< Waits; returns what it waited. */
    uint32_t (*stall_microseconds)(void *context, uint32_t microseconds); /**< Busy-waits; returns what it waited. */
    uint64_t (*clock_100ns)(void *context);                               /**< Reads the clock. */
    void *(*allocate_pages)(void *context, uint32_t count);               /**< Gives pages of memory. */
    void (*release_pages)(void *context, void *address, uint32_t count);  /**< Takes them back. */
} AdvancedConfigurationAndPowerInterfaceHost_t;

/**
 * @brief The host the boot reads this machine's firmware through.
 */
extern const AdvancedConfigurationAndPowerInterfaceHost_t advanced_configuration_and_power_interface_machine_host;

#endif /* KERNEL_ACPI_HOST_H */
