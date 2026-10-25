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
 * @file machine_language_regions.h
 * @brief What reaching a region holds: the mapping cache, and what the field engine counted.
 *
 * Part of layer L4 of the specification (docs/acpi/specification.md, §4.4 and §5.3). A field unit
 * is read and written one datum of its access width at a time, through its region's space: memory
 * through the page each access touches, mapped once and kept in a cache, and I/O ports through the
 * host, the ports the kernel drives protected.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-25
 **************************************************************************/

#ifndef KERNEL_ACPI_MACHINE_LANGUAGE_REGIONS_H
#define KERNEL_ACPI_MACHINE_LANGUAGE_REGIONS_H

#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_MAPPINGS                                       \
    64u /**< Entries of the region mapping cache (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SPACES                                             \
    256u /**< Region spaces, as AML encodes them in a byte. */

/**
 * @brief How a mapping of the cache was made.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageMappingKind {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAPPING_CACHED = 1, /**< map_firmware_writable. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAPPING_DEVICE = 2, /**< map_device. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageMappingKind_t;

/**
 * @brief The page one access touched, mapped once (§5.3).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageMapping {
    uint64_t physical_address; /**< Its first byte. */
    void *address;             /**< Where the host mapped it: the only pointer, never folded. */
    uint32_t last_use;         /**< When it was last used, by the cache's own clock. */
    uint8_t kind;              /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAPPING_* kind. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageMapping_t;

/**
 * @brief The mapping cache of a load, and what its field accesses counted (§5.3, §7.3 O11).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions {
    AdvancedConfigurationAndPowerInterfaceMachineLanguageMapping_t
        mappings[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_MAPPINGS]; /**< The cache. */
    uint32_t mapping_count;                                                                 /**< Entries in use. */
    uint32_t clock;                   /**< Ticks once per access, for the least recently used entry. */
    uint32_t field_reads;             /**< Field units read. */
    uint32_t field_writes;            /**< Field units written. */
    uint32_t map_calls;               /**< Maps the cache asked the host for. */
    uint32_t mapping_cache_hits;      /**< Accesses the cache already mapped. */
    uint32_t split_qword_accesses;    /**< 64-bit data made as two 32-bit accesses, low then high. */
    uint32_t unaligned_accesses;      /**< Memory accesses at an address their width does not divide, made byte by
                                           byte. */
    uint32_t protected_port_accesses; /**< Accesses to a protected port: 0 read, writes dropped. */
    uint32_t region_lengths_rounded;  /**< Accesses past a region's length but within it rounded up to the access
                                           width. */
    uint32_t space_unsupported[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SPACES]; /**< Accesses
                                                                                                        refused, per
                                                                                                        space. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t;

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_REGIONS_H */
