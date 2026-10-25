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
 * @file simulated_host.h
 * @brief A host whose firmware is bytes placed at physical addresses of its own.
 *
 * The ACPI tests read made-up firmware through it in ring 0 and on the host alike, and the host
 * harness reads the firmware of recorded machines through it, so that both sides run the same
 * code on the same bytes.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_SIMULATED_HOST_H
#define KERNEL_TESTS_ACPI_SIMULATED_HOST_H

#include <kernel/acpi/host.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_RANGES                                           \
    128u /**< Ranges one simulated host holds: a recorded machine's tables, each placed once. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_MAPS                                             \
    128u /**< Maps one simulated host keeps open at once: a full directory, and the one being opened. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_PAGES                                            \
    64u /**< Pages of simulated memory one host holds once written. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_MEMORY_TYPES                                     \
    16u /**< Ranges of the memory map one simulated host describes. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_PORTS                                            \
    32u /**< I/O ports one simulated host gives a value to read. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES                                         \
    256u /**< Accesses the log keeps: the first ones, the others only counted. */

/**
 * @brief Bytes a simulated host serves at a physical address.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedRange {
    uint64_t physical_address; /**< Physical address of the first byte. */
    uint64_t length;           /**< Length in bytes. */
    const uint8_t *bytes;      /**< The bytes, which the caller owns and keeps until the host is no longer used. */
} AdvancedConfigurationAndPowerInterfaceSimulatedRange_t;

/**
 * @brief Which call gave a map, so that only its own unmap takes it back.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceSimulatedMapKind {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_FIRMWARE = 0,          /**< map_firmware. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_FIRMWARE_WRITABLE = 1, /**< map_firmware_writable. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_DEVICE = 2,            /**< map_device. */
} AdvancedConfigurationAndPowerInterfaceSimulatedMapKind_t;

/**
 * @brief A map a simulated host gave and that has not been unmapped.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedMap {
    const void *address;       /**< What the map returned. */
    uint64_t length;           /**< The length it was given. */
    uint64_t physical_address; /**< The physical address it was given. */
    uint8_t kind;              /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_* kind. */
} AdvancedConfigurationAndPowerInterfaceSimulatedMap_t;

/**
 * @brief A page of simulated memory, taken at its first write: its bytes start as the ranges placed
 *        and the fill byte give them.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedPage {
    uint64_t physical_address; /**< Its first byte. */
    uint8_t *bytes;            /**< Its 4096 bytes, from kernel_vmm_alloc_pages. */
} AdvancedConfigurationAndPowerInterfaceSimulatedPage_t;

/**
 * @brief A range of the memory map a simulated host describes.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedMemoryType {
    uint64_t physical_address; /**< Its first byte. */
    uint64_t length;           /**< Its length in bytes. */
    uint8_t type;              /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_* type. */
} AdvancedConfigurationAndPowerInterfaceSimulatedMemoryType_t;

/**
 * @brief What an access of the log did.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceSimulatedAccessKind {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MEMORY_READ = 1,  /**< read_memory. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MEMORY_WRITE = 2, /**< write_memory. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_READ = 3,    /**< port_read. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_WRITE = 4,   /**< port_write. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_EXCHANGE = 5,     /**< compare_exchange: value is the
                                                                               word it left. */
} AdvancedConfigurationAndPowerInterfaceSimulatedAccessKind_t;

/**
 * @brief One access the simulated host was asked for, in order.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedAccess {
    uint8_t kind;     /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_* access kind. */
    uint8_t width;    /**< Its width in bytes. */
    uint16_t port;    /**< The port, for a port access. */
    uint32_t value;   /**< The value read or written. */
    uint64_t address; /**< The physical address, for a memory access. */
} AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t;

/**
 * @brief The value a simulated I/O port reads as; every other port reads as 0.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedPort {
    uint16_t port;  /**< The port. */
    uint32_t value; /**< What a read of it gives, cut to the read's width. */
} AdvancedConfigurationAndPowerInterfaceSimulatedPort_t;

/**
 * @brief A host whose physical memory is the ranges placed in it: a map succeeds when one range
 *        holds every byte asked for, and an unmap must name an open map by its address and length.
 *
 * @details It points at itself through host.context, so it is used where it was opened and never
 *          copied. Memory mapped writable or uncached is reached through read_memory and
 *          write_memory alone: what the ranges hold, or the fill byte, until a write takes the page;
 *          each of its maps returns an address of its own that is never read, and an access whose
 *          address and physical address disagree with an open map is counted. Physical address 0
 *          cannot be mapped, as in ring 0. Its I/O space reads as 0 but for the ports
 *          given a value, and ignores writes. Every access is logged in order. Its clock is virtual:
 *          sleep_milliseconds advances it by what is asked, or by whole ticks of tick_milliseconds,
 *          at least one, when that is not 0.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedHost {
    AdvancedConfigurationAndPowerInterfaceHost_t host; /**< What the ACPI layer is given. */
    uint64_t root_system_description_pointer;          /**< What the host gives as the root pointer, or 0. */
    uint32_t outstanding_maps;                         /**< Maps not unmapped yet, each kept in maps. */
    uint32_t mismatched_unmaps;                        /**< Unmaps whose address and length name no open map. */
    uint32_t range_count;                              /**< Ranges placed. */
    uint32_t outstanding_pages;                        /**< Pages given and not taken back yet. */
    uint8_t memory_fill;                               /**< What memory no range covers reads as. */
    uint32_t map_serial;                               /**< Writable or uncached maps made, each a distinct address. */
    uint32_t inconsistent_accesses;                    /**< Memory accesses whose address is not where a map of their
                                                            physical address puts it. */
    uint32_t page_count;                               /**< Pages of simulated memory taken. */
    uint32_t writes_dropped;                           /**< Memory writes with no page left to take. */
    uint32_t memory_type_count;                        /**< Ranges of the memory map described. */
    uint32_t port_count;                               /**< Ports given a value. */
    uint32_t access_count;                             /**< Accesses made, logged or not. */
    uint64_t firmware_lock_address;                    /**< The global lock word the simulated firmware shares, or 0. */
    uint32_t firmware_holds_for_reads;     /**< Reads of that word the firmware keeps it owned for, then releases it. */
    bool firmware_wants_lock;              /**< Whether the firmware asks for the lock, setting pending, as soon as the
                                                OS takes it; asked once. */
    uint64_t clock_milliseconds;           /**< The virtual clock, which sleep_milliseconds alone advances. */
    uint32_t tick_milliseconds;            /**< The period of the tick a sleep waits whole, or 0 for no tick. */
    uint16_t firmware_command_port;        /**< The SMI command port the simulated firmware answers, or 0. */
    uint8_t firmware_enable_command;       /**< The ACPI_ENABLE it answers there by setting SCI_EN. */
    uint16_t firmware_control_port;        /**< The PM1 control port whose SCI_EN it sets. */
    uint32_t firmware_enable_milliseconds; /**< Virtual milliseconds from ACPI_ENABLE to SCI_EN reading set;
                                                UINT32_MAX for never. */
    uint64_t firmware_enabled_at;          /**< When SCI_EN reads set; UINT64_MAX until ACPI_ENABLE is written. */
    AdvancedConfigurationAndPowerInterfaceSimulatedRange_t
        ranges[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_RANGES]; /**< In the order placed. */
    AdvancedConfigurationAndPowerInterfaceSimulatedMap_t
        maps[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_MAPS]; /**< The open maps, in no order. */
    AdvancedConfigurationAndPowerInterfaceSimulatedPage_t
        pages[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_PAGES]; /**< In the order taken. */
    AdvancedConfigurationAndPowerInterfaceSimulatedMemoryType_t
        memory_types[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_MEMORY_TYPES]; /**< The memory
                                                                                                      map; reserved
                                                                                                      elsewhere. */
    AdvancedConfigurationAndPowerInterfaceSimulatedPort_t
        ports[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_PORTS]; /**< The ports given a value. */
    AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t
        accesses[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES]; /**< The first accesses. */
} AdvancedConfigurationAndPowerInterfaceSimulatedHost_t;

/**
 * @brief Empties a simulated host: no range, no root pointer, nothing mapped, no page given. Its pages
 *        come from kernel_vmm_alloc_pages, which the host harness gives too.
 *
 * @param simulated The host, which must stay where it is from now on.
 */
extern void advanced_configuration_and_power_interface_simulated_host_open(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated);

/**
 * @brief Gives back the pages of simulated memory a host took; the host stays usable, its memory as
 *        the ranges and the fill byte give it.
 *
 * @param simulated The host.
 */
extern void advanced_configuration_and_power_interface_simulated_host_close(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated);

/**
 * @brief Describes a range of the memory map: memory_type answers @p type inside it.
 *
 * @param simulated        The host.
 * @param physical_address Its first byte.
 * @param length           Its length in bytes.
 * @param type             An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_* type.
 * @return false when the host describes as many ranges as it can.
 */
extern bool advanced_configuration_and_power_interface_simulated_host_describe(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint64_t physical_address, uint64_t length,
    uint8_t type);

/**
 * @brief Gives an I/O port the value its reads give.
 *
 * @param simulated The host.
 * @param port      The port.
 * @param value     What a read gives, cut to its width.
 * @return false when the host gives values to as many ports as it can.
 */
extern bool advanced_configuration_and_power_interface_simulated_host_set_port(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint16_t port, uint32_t value);

/**
 * @brief Reads simulated memory as read_memory would, without logging it.
 *
 * @param simulated        The host.
 * @param physical_address The first byte.
 * @param width            Bytes to read, 1 to 8, little-endian.
 * @return What the bytes hold.
 */
extern uint64_t advanced_configuration_and_power_interface_simulated_host_peek(
    const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint64_t physical_address, uint8_t width);

/**
 * @brief Writes simulated memory as write_memory would, without logging it.
 *
 * @param simulated        The host.
 * @param physical_address The first byte.
 * @param bytes            What to write.
 * @param length           How many bytes.
 * @return false when a page could not be taken, the bytes before it written.
 */
extern bool advanced_configuration_and_power_interface_simulated_host_poke(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint64_t physical_address, const void *bytes,
    uint32_t length);

/**
 * @brief Serves @p length bytes at @p physical_address.
 *
 * @param simulated        The host.
 * @param physical_address Physical address of the first byte.
 * @param bytes            The bytes, kept by the caller as long as the host is used.
 * @param length           Length in bytes, at least 1.
 * @return false when the range is empty, wraps, overlaps one already placed, or the host is full.
 */
extern bool advanced_configuration_and_power_interface_simulated_host_place(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint64_t physical_address, const void *bytes,
    uint64_t length);

#endif /* KERNEL_TESTS_ACPI_SIMULATED_HOST_H */
