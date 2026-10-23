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
 * @file acpi.h
 * @brief ACPI table discovery and the MADT: local APICs, I/O APICs and interrupt overrides.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-09
 **************************************************************************/

#ifndef KERNEL_CPU_ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_H
#define KERNEL_CPU_ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_H

#include <kernel/cpu/paging.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_IOAPIC_COUNT     8u
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_ISO_COUNT        16u
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_LOCAL_APIC_COUNT 32u
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_TABLE_COUNT      64u /**< Tables one directory records. */

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} AdvancedConfigurationAndPowerInterfaceRsdp_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} AdvancedConfigurationAndPowerInterfaceSdtHeader_t;

/**
 * @struct AdvancedConfigurationAndPowerInterfaceTable
 * @brief One table of the firmware, as a directory records it.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceTable {
    char signature[4];                                               /**< Its four letters, such as "APIC". */
    uint32_t physical_address;                                       /**< Where the firmware put it. */
    uint32_t length;                                                 /**< Its length in bytes, header included. */
    uint8_t revision;                                                /**< Its revision. */
    bool checksum_valid;                                             /**< Whether its bytes sum to zero. */
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header; /**< Its firmware window, every byte mapped. */
} AdvancedConfigurationAndPowerInterfaceTable_t;

/**
 * @struct AdvancedConfigurationAndPowerInterfaceDirectory
 * @brief Every table a root system description pointer leads to: the entries of its root table,
 *        then the DSDT the FADT names.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceDirectory {
    bool walked;                                /**< Whether a valid root table was found and walked. */
    uint8_t rsdp_revision;                      /**< 0 for ACPI 1.0; 2 or more when the pointer may name an XSDT. */
    bool extended_root;                         /**< Whether the entries came from the XSDT, not the RSDT. */
    uint32_t root_physical_address;             /**< The root table walked. */
    uint32_t table_count;                       /**< Tables recorded. */
    uint32_t unreachable_count;                 /**< Tables above 4 GiB, past the capacity, or not mappable. */
    uint32_t firmware_control_physical_address; /**< The FACS, which has no table header, or 0: the 32-bit
                                                     FIRMWARE_CTRL when it is not 0, else X_FIRMWARE_CTRL. */
    AdvancedConfigurationAndPowerInterfaceTable_t
        tables[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_TABLE_COUNT]; /**< In the order they were found. */
} AdvancedConfigurationAndPowerInterfaceDirectory_t;

typedef struct __attribute__((packed)) {
    AdvancedConfigurationAndPowerInterfaceSdtHeader_t header;
    uint32_t local_apic_address;
    uint32_t flags;
} AdvancedConfigurationAndPowerInterfaceMadt_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
} AdvancedConfigurationAndPowerInterfaceMadtEntryHeader_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} AdvancedConfigurationAndPowerInterfaceMadtLocalApicEntry_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
} AdvancedConfigurationAndPowerInterfaceMadtIoApicEntry_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint64_t local_apic_address;
} AdvancedConfigurationAndPowerInterfaceMadtLocalApicAddressOverrideEntry_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint8_t bus;
    uint8_t source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} AdvancedConfigurationAndPowerInterfaceMadtInterruptSourceOverrideEntry_t;

typedef struct {
    uint8_t id;
    uint32_t physical_base;
    uint32_t gsi_base;
} AdvancedConfigurationAndPowerInterfaceIoApicInfo_t;

typedef struct {
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} AdvancedConfigurationAndPowerInterfaceLocalApicInfo_t;

typedef struct {
    uint8_t bus;
    uint8_t source_irq;
    uint32_t global_system_interrupt;
    uint16_t flags;
} AdvancedConfigurationAndPowerInterfaceInterruptSourceOverrideInfo_t;

typedef struct __attribute__((packed)) {
    AdvancedConfigurationAndPowerInterfaceSdtHeader_t header;
    uint32_t reserved1;
    uint64_t reserved2;
} AdvancedConfigurationAndPowerInterfaceSrat_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
} AdvancedConfigurationAndPowerInterfaceSratEntryHeader_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint8_t proximity_domain_low;
    uint8_t apic_id;
    uint32_t flags;
    uint8_t local_sapic_eid;
    uint8_t proximity_domain_high[3];
    uint32_t clock_domain;
} AdvancedConfigurationAndPowerInterfaceSratLocalApicEntry_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint32_t proximity_domain;
    uint16_t reserved1;
    uint64_t base_address;
    uint64_t region_length;
    uint32_t reserved2;
    uint32_t flags;
    uint64_t reserved3;
} AdvancedConfigurationAndPowerInterfaceSratMemoryEntry_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint16_t reserved1;
    uint32_t proximity_domain;
    uint32_t x2apic_id;
    uint32_t flags;
    uint32_t clock_domain;
    uint32_t reserved2;
} AdvancedConfigurationAndPowerInterfaceSratX2ApicEntry_t;

/**
 * @brief Parse ACPI RSDP/RSDT and discover MADT topology metadata.
 *
 * Current scope is discovery-only (no IOAPIC programming yet).
 */
extern void advanced_configuration_and_power_interface_madt_initialize(void);

/**
 * @brief Walks the tables a root system description pointer leads to, each mapped through a
 *        firmware window.
 *
 * @details The XSDT is walked when the pointer's revision is 2 or more and it names one below 4 GiB
 *          whose signature and checksum hold; the RSDT otherwise, which is also what an XSDT that
 *          fails its checksum falls back to. An XSDT entry above 4 GiB is counted unreachable: this
 *          kernel maps 32-bit physical addresses. The DSDT is then taken from the FADT, whose 64-bit
 *          X_DSDT wins when the FADT is long enough to hold it and it is not 0, as in ACPICA's
 *          AcpiTbSelectAddress, unless it lies above 4 GiB: the 32-bit DSDT is taken then.
 *
 * @param rsdp_physical_address Physical address of the root system description pointer.
 * @param out_directory         Emptied, then filled with every table found.
 * @return false when the pointer is invalid, or names no valid root table.
 */
extern bool advanced_configuration_and_power_interface_read_directory(
    uint32_t rsdp_physical_address, AdvancedConfigurationAndPowerInterfaceDirectory_t *out_directory);

/**
 * @brief Unmaps every table of a directory advanced_configuration_and_power_interface_read_directory
 *        filled, and empties it.
 *
 * @param directory The directory, whose table headers must not be used afterwards.
 */
extern void advanced_configuration_and_power_interface_release_directory(
    AdvancedConfigurationAndPowerInterfaceDirectory_t *directory);

/**
 * @brief The directory of the firmware's tables, read on the first call from the root pointer found
 *        in the EBDA or the BIOS area.
 *
 * @return The directory, whose walked field is false when no valid root table was found.
 */
extern const AdvancedConfigurationAndPowerInterfaceDirectory_t *
advanced_configuration_and_power_interface_directory(void);

/**
 * @brief Finds a table by its signature in a directory.
 *
 * @details A table whose checksum fails is still given out when no table with @p signature has one
 *          that holds, as ACPICA and uACPI do by default: dropping it can drop the DSDT, an SSDT
 *          or the MADT of a machine whose firmware got one byte wrong. Its record says so.
 *
 * @param directory The directory to search.
 * @param signature Four-character signature of the table, such as "SRAT" or "DSDT".
 * @return The header of the first table with @p signature whose checksum holds, else of the first
 *         whose checksum fails, or NULL when the directory has none.
 */
extern const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *
advanced_configuration_and_power_interface_find_in_directory(
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory, const char *signature);

/**
 * @brief Finds a table of the firmware by its signature, the DSDT included.
 *
 * @param signature Four-character signature of the table, such as "SRAT" or "DSDT".
 * @return The header of the table, every byte mapped, chosen as
 *         advanced_configuration_and_power_interface_find_in_directory chooses, or NULL when the
 *         firmware has no such table.
 */
extern const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *
advanced_configuration_and_power_interface_find_table(const char *signature);

/**
 * @brief Return current ACPI/MADT discovery state name.
 */
extern const char *advanced_configuration_and_power_interface_madt_get_state_name(void);

/**
 * @brief Return non-zero when MADT was parsed successfully.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_is_available(void);

/**
 * @brief Return MADT-reported local APIC physical base address.
 */
extern uint32_t advanced_configuration_and_power_interface_madt_get_local_apic_physical_base(void);

/**
 * @brief Return discovered Local APIC count from MADT entries.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_get_local_apic_count(void);

/**
 * @brief Return discovered Local APIC id for an index.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_get_local_apic_id(uint8_t index);

/**
 * @brief Return non-zero when Local APIC entry is MADT-enabled (flags bit0).
 */
extern uint8_t advanced_configuration_and_power_interface_madt_is_local_apic_enabled(uint8_t index);

/**
 * @brief Return number of MADT Local APIC entries with enabled flag set.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_get_enabled_local_apic_count(void);

/**
 * @brief Return discovered IOAPIC count from MADT entries.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_get_io_apic_count(void);

/**
 * @brief Return discovered IOAPIC id for an index.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_get_io_apic_id(uint8_t index);

/**
 * @brief Return discovered IOAPIC MMIO physical base for an index.
 */
extern uint32_t advanced_configuration_and_power_interface_madt_get_io_apic_physical_base(uint8_t index);

/**
 * @brief Return discovered IOAPIC GSI base for an index.
 */
extern uint32_t advanced_configuration_and_power_interface_madt_get_io_apic_gsi_base(uint8_t index);

/**
 * @brief Return discovered MADT Interrupt Source Override count.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count(void);

/**
 * @brief Return ISA bus id for an Interrupt Source Override index.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_bus(uint8_t index);

/**
 * @brief Return source IRQ line for an Interrupt Source Override index.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_source_irq(uint8_t index);

/**
 * @brief Return remapped global system interrupt for an ISO index.
 */
extern uint32_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_gsi(uint8_t index);

/**
 * @brief Return raw MADT ISO flags for an index.
 */
extern uint16_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_flags(uint8_t index);

/**
 * @brief Resolve an ISA IRQ line to effective GSI and flags using MADT ISO data.
 *
 * If no matching ISO entry is found, routing falls back to identity mapping:
 * GSI=irq and flags=0.
 *
 * @return non-zero when MADT topology is available; zero otherwise.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_resolve_isa_irq(uint8_t irq,
                                                                               uint32_t *global_system_interrupt,
                                                                               uint16_t *flags);

/**
 * @brief Select the IOAPIC index responsible for a GSI using MADT bases.
 *
 * Selection rule: choose the IOAPIC with the greatest `gsi_base` not greater
 * than the requested GSI.
 *
 * @return non-zero when a candidate IOAPIC was found; zero otherwise.
 */
extern uint8_t advanced_configuration_and_power_interface_madt_find_io_apic_for_gsi(uint32_t global_system_interrupt,
                                                                                    uint8_t *io_apic_index);

#endif /* KERNEL_CPU_ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_H */
