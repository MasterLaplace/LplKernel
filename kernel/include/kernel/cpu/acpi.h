/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** acpi
*/

#ifndef ACPI_H_
#define ACPI_H_

#include <kernel/cpu/paging.h>

#include <stddef.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_IOAPIC_COUNT 8u
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_ISO_COUNT    16u
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_LOCAL_APIC_COUNT 32u

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
 * @brief Find an ACPI table by its signature.
 * 
 * @param signature 4-character signature of the table (e.g. "SRAT").
 * @return Pointer to the mapped ACPI table header, or NULL if not found.
 */
extern const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *advanced_configuration_and_power_interface_find_table(const char *signature);

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
extern uint8_t advanced_configuration_and_power_interface_madt_resolve_isa_irq(uint8_t irq, uint32_t *global_system_interrupt,
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

#endif /* !ACPI_H_ */
