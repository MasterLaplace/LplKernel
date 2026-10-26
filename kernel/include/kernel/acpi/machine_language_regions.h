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

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_MAPPINGS                                       \
    64u /**< Entries of the region mapping cache (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SPACES                                             \
    256u /**< Region spaces, as AML encodes them in a byte. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PERIPHERAL_COMPONENT_INTERCONNECT_CONFIGURATION    \
    0x02u /**< The RegionSpace byte of PCI_Config, the space a _REG is told of (§3.8 step 3). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_GLOBAL_LOCK_POLLS                                  \
    65536u /**< Reads of the global lock an acquisition waits for the firmware to release it. */

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
    uint32_t field_reads;             /**< Field unit reads begun, a failed one included. */
    uint32_t field_writes;            /**< Field unit writes begun, a failed one included. */
    uint32_t map_calls;               /**< Maps the cache asked the host for. */
    uint32_t mapping_cache_hits;      /**< Accesses the cache already mapped. */
    uint32_t split_qword_accesses;    /**< 64-bit data made as two 32-bit accesses, low then high. */
    uint32_t unaligned_accesses;      /**< Memory accesses at an address their width does not divide, made byte by
                                           byte. */
    uint32_t protected_port_accesses; /**< Accesses to a protected port: 0 read, writes dropped. */
    uint32_t region_lengths_rounded;  /**< Accesses past a region's length but within it rounded up to the access
                                           width. */
    uint32_t field_accesses_peak;     /**< Most field accesses one table load made (§6.1). */
    uint32_t space_unsupported[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SPACES]; /**< Accesses
                                                                                                        refused, per
                                                                                                        space. */
    uint64_t global_lock_address; /**< The physical address of the FACS's GlobalLock word, or 0 without a global
                                       lock. */
    uint8_t *global_lock_word;    /**< Where the lock word is mapped, for the whole load, or NULL: a pointer, never
                                       folded. */
    uint8_t
        global_lock_mapping_kind; /**< How its page is mapped: an
                                       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAPPING_* kind. */
    bool facs_refused;            /**< Whether a FACS was given but could not be used: misaligned, in usable memory,
                                       without its signature, too short, or not mapped. */
    uint16_t power_management_control_ports[2]; /**< The PM1a and PM1b control ports GBL_RLS is written to, or 0. */
    uint32_t global_lock_depth;                 /**< Accesses in progress that hold the global lock. */
    uint32_t global_lock_acquisitions;          /**< Times the global lock was taken from the firmware's side. */
    uint32_t global_lock_waits;                 /**< Of those, the ones that found the firmware owning it and waited. */
    uint32_t global_lock_releases_signalled;    /**< Releases that wrote GBL_RLS to a PM1 control register, the
                                                     firmware waiting for the lock. */
    uint32_t global_lock_timeouts;              /**< Acquisitions given up, the firmware never releasing the lock. */
    uint32_t unlocked_lock_accesses;    /**< Lock fields accessed without a global lock: the platform has none, or
                                             its FACS was refused (facs_refused). */
    uint32_t pci_attachments;           /**< PCI_Config regions attached: their address resolved (§4.4). */
    uint32_t internal_evaluations;      /**< Methods evaluated in internal frames for those attaches (§3.6, O13). */
    uint32_t internal_failures;         /**< Of those, the ones that failed, their object taken as absent. */
    uint32_t pci_protected_writes;      /**< Writes to the predefined header of a PCI function, dropped. */
    uint32_t pci_lost_accesses;         /**< Accesses to a function the attach could not reach: all ones read,
                                             writes dropped. */
    uint32_t pci_ecam_accesses;         /**< Accesses past offset 0xFF or in a segment other than 0, which need ECAM
                                             (#58) and fail with address_unreachable. */
    bool registers_at_creation;         /**< Whether a PCI_Config region a method creates gets its _REG at once: true
                                             once the initialisation has run the _REG of every scope (§3.8 step 3). */
    uint32_t pci_registrations;         /**< _REG (PCI_Config, 1) run at once, in an internal frame, for a region a
                                             method created (§5.3). */
    uint32_t pci_registration_failures; /**< Of those, the ones that failed and were dropped, and the _REG that
                                             were not called because they do not take two arguments. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t;

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_REGIONS_H */
