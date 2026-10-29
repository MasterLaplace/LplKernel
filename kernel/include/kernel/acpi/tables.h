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
 * @file tables.h
 * @brief The firmware's ACPI tables: the directory of every table, the normalised FADT, and the MADT's
 *        processors, NMI connections, I/O APICs and interrupt overrides.
 *
 * Architecture-free: the tables are mapped, and the root system description pointer found, through
 * the host interface of kernel/acpi/host.h.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_ACPI_TABLES_H
#define KERNEL_ACPI_TABLES_H

#include <kernel/acpi/host.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_IOAPIC_COUNT                                                    \
    64u /**< I/O APICs one MADT reading keeps, as many as Linux keeps on 32-bit x86. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_ISO_COUNT                                                       \
    16u /**< Interrupt source overrides one MADT reading keeps: one per ISA interrupt. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_PROCESSOR_COUNT                                                 \
    256u /**< CPU slots one MADT reading keeps; the largest server of the corpus enables 208. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_NON_MASKABLE_INTERRUPT_COUNT                                    \
    256u /**< NMI connections one MADT reading keeps; firmware lists up to one per processor. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MADT_ENABLED 0x1u /**< Local APIC flag: ready for use. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MADT_ONLINE_CAPABLE                                                 \
    0x2u /**< Local APIC flag: not enabled, but can be enabled at run time. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ALL_PROCESSORS                                                      \
    0xFFFFFFFFu /**< The processor UID of an NMI connection that every processor has. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_TABLE_COUNT 64u /**< Tables one directory records. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ROOT_SYSTEM_DESCRIPTION_POINTER_SIGNATURE                           \
    "RSD PTR " /**< A root pointer's 8 letters. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ROOT_SYSTEM_DESCRIPTION_POINTER_VERSION_1_LENGTH                    \
    20u /**< Bytes a root pointer's first checksum covers. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_FIXED_DESCRIPTION_LENGTH                                            \
    276u /**< Bytes of a FADT of revision 6, the longest this layer reads: a longer FADT is read that far. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_MEMORY                                                 \
    0u /**< The address space of a Generic Address Structure in memory. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_INPUT_OUTPUT                                           \
    1u /**< The address space of a Generic Address Structure in the I/O space. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_MAXIMUM_POLLS                                                  \
    3000u /**< Reads of SCI_EN the switch to ACPI mode makes at most, 1 ms apart. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_MAXIMUM_MILLISECONDS                                           \
    3000u /**< Milliseconds the switch to ACPI mode waits between them at most, as the host counts them. */

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
 * @brief A register of the fixed hardware, as a Generic Address Structure: a FADT's legacy block
 *        becomes one in the I/O space.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceGenericAddress {
    uint64_t address; /**< Where the register lies in its space; 0 when the FADT gives none. */
    uint8_t space;    /**< Its address space, such as ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_MEMORY. */
    uint8_t bit_width;   /**< Its width in bits, at most 255. */
    uint8_t bit_offset;  /**< Its first bit. */
    uint8_t access_size; /**< 0 for any access, 1 to 4 for byte, word, double-word and quad-word accesses. */
} AdvancedConfigurationAndPowerInterfaceGenericAddress_t;

/**
 * @brief The fields of a FADT this layer reads, normalised once (§3.9).
 *
 * @details They are read from a zeroed local copy of at most
 *          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_FIXED_DESCRIPTION_LENGTH bytes, so that a field
 *          past the firmware's length reads 0. A legacy register block becomes a Generic Address
 *          Structure in the I/O space, unless its extended block gives an address. The bytes ACPI 1.0
 *          reserved read 0 in a FADT of revision 2 or less, the reset register is absent unless
 *          RESET_REG_SUP is set, and a hardware-reduced FADT reads 0 in every field ACPI tells OSPM
 *          to ignore on such a platform.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceFixedDescription {
    bool present;           /**< Whether a FADT was read; every other field is 0 when none was. */
    bool hardware_reduced;  /**< HW_REDUCED_ACPI: the platform has no fixed hardware, nor a global lock. */
    uint8_t revision;       /**< The earliest revision as long as the FADT: 1, 2, 3, 5 or 6. */
    uint8_t enable_command; /**< ACPI_ENABLE: what SMI_CMD is written to hand the fixed hardware to the OS, 0 on a
                                 platform without legacy mode. */
    uint8_t performance_state_command;              /**< PSTATE_CNT: what SMI_CMD is written to take over the
                                                         performance states. */
    uint8_t idle_state_command;                     /**< CST_CNT: what SMI_CMD is written once `_CST` is used. */
    uint8_t reset_value;                            /**< RESET_VALUE: what the reset register is written. */
    uint8_t general_purpose_event_1_base;           /**< GPE1_BASE: the number of GPE1's first event. */
    uint8_t general_purpose_event_block_lengths[2]; /**< GPE0_BLK_LEN and GPE1_BLK_LEN in bytes, which a Generic
                                                         Address Structure cannot hold past 31; the extended block's
                                                         width when the legacy length is 0. */
    uint16_t system_control_interrupt;              /**< SCI_INT: the SCI's 8259 line, or its global system
                                                         interrupt on a platform without an 8259. */
    uint32_t length;                                /**< Bytes of the firmware's FADT. */
    uint32_t flags;                                 /**< Its Flags, without those a hardware-reduced platform
                                                         ignores. */
    uint32_t system_management_interrupt_command;   /**< SMI_CMD: the I/O port of the SMI command, 0 without one. */
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t
        power_management_event_blocks[2]; /**< PM1a_EVT_BLK and PM1b_EVT_BLK: the status, then the enable register. */
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t
        power_management_control_blocks[2]; /**< PM1a_CNT_BLK and PM1b_CNT_BLK. */
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t power_management_timer_block; /**< PM_TMR_BLK. */
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t
        general_purpose_event_blocks[2]; /**< GPE0_BLK and GPE1_BLK: the status, then the enable registers. */
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t reset_register;         /**< RESET_REG. */
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t sleep_control_register; /**< SLEEP_CONTROL_REG. */
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t sleep_status_register;  /**< SLEEP_STATUS_REG. */
} AdvancedConfigurationAndPowerInterfaceFixedDescription_t;

/**
 * @brief How the switch to ACPI mode ended (§3.8 step 0).
 */
typedef enum AdvancedConfigurationAndPowerInterfaceModeOutcome {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NOT_RUN = 0,          /**< No FADT, or no switch yet. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_HARDWARE_REDUCED = 1, /**< A hardware-reduced platform, which
                                                                               boots in ACPI mode: no access made. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_UNREACHABLE = 2,      /**< A PM1 control block, SMI_CMD, or a block
                                                                               whose enables it clears is not wholly
                                                                               in the I/O space, or reaches a
                                                                               protected port: nothing written. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_ALREADY = 3,          /**< SCI_EN was set: nothing written. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NO_COMMAND_PORT = 4,  /**< SMI_CMD is 0: nothing written. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NO_LEGACY_MODE = 5,   /**< ACPI_ENABLE is 0, which a platform
                                                                               without legacy mode gives: nothing
                                                                               written. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_SWITCHED = 6,         /**< ACPI_ENABLE written, and SCI_EN set
                                                                               within the bound. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_FAILED = 7,           /**< ACPI_ENABLE written, and SCI_EN still
                                                                               clear past the bound: legacy mode. */
} AdvancedConfigurationAndPowerInterfaceModeOutcome_t;

/**
 * @brief What one switch to ACPI mode did.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceModeSwitch {
    uint8_t outcome;                   /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_* outcome. */
    uint32_t enable_registers_cleared; /**< GPE and PM1 enable registers written before ACPI_ENABLE. */
    uint32_t polls;                    /**< Reads of SCI_EN once ACPI_ENABLE was written. */
    uint32_t waited_milliseconds;      /**< Milliseconds the host waited between them, as it counts them. */
} AdvancedConfigurationAndPowerInterfaceModeSwitch_t;

/**
 * @struct AdvancedConfigurationAndPowerInterfaceDirectory
 * @brief Every table a root system description pointer leads to: the entries of its root table,
 *        then the DSDT the FADT names.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceDirectory {
    const AdvancedConfigurationAndPowerInterfaceHost_t *host; /**< The host its tables are mapped through. */
    bool walked;                                              /**< Whether a valid root table was found and walked. */
    uint8_t rsdp_revision;          /**< 0 for ACPI 1.0; 2 or more when the pointer may name an XSDT. */
    bool extended_root;             /**< Whether the entries came from the XSDT, not the RSDT. */
    uint32_t root_physical_address; /**< The root table walked. */
    uint32_t table_count;           /**< Tables recorded. */
    uint32_t
        unreachable_count; /**< Tables above 4 GiB, the root pointer included, past the capacity, or not mappable. */
    uint32_t firmware_control_physical_address; /**< The FACS, which has no table header, or 0: the 32-bit
                                                     FIRMWARE_CTRL when it is not 0, else X_FIRMWARE_CTRL. */
    AdvancedConfigurationAndPowerInterfaceFixedDescription_t
        fixed_description; /**< The FADT the directory gives, normalised; not present without one. */
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

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint8_t processor_unique_identifier; /**< 0xFF for every processor. */
    uint16_t flags;
    uint8_t local_interrupt;
} AdvancedConfigurationAndPowerInterfaceMadtLocalApicNonMaskableInterruptEntry_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint32_t x2apic_id;
    uint32_t flags;
    uint32_t processor_unique_identifier;
} AdvancedConfigurationAndPowerInterfaceMadtLocalX2ApicEntry_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
    uint16_t flags;
    uint32_t processor_unique_identifier; /**< 0xFFFFFFFF for every processor. */
    uint8_t local_interrupt;
    uint8_t reserved[3];
} AdvancedConfigurationAndPowerInterfaceMadtLocalX2ApicNonMaskableInterruptEntry_t;

typedef struct {
    uint8_t id;
    uint32_t physical_base;
    uint32_t gsi_base;
} AdvancedConfigurationAndPowerInterfaceIoApicInfo_t;

/**
 * @brief A CPU slot: a processor whose MADT entry, local APIC or local x2APIC, has its Enabled flag
 *        set.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceProcessor {
    uint32_t processor_unique_identifier; /**< Its ACPI Processor UID, which the `_UID` of its processor device or
                                               the ProcessorId of its `Processor` declaration equals. */
    uint32_t apic_id;                     /**< Its local APIC ID, or its x2APIC ID. */
} AdvancedConfigurationAndPowerInterfaceProcessor_t;

/**
 * @brief The local interrupt input a processor's NMI is wired to, from a local APIC NMI or a local
 *        x2APIC NMI entry.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceProcessorNonMaskableInterrupt {
    uint32_t processor_unique_identifier; /**< Its processor's UID, or the value that means every one. */
    uint16_t flags;                       /**< MPS INTI flags: polarity and trigger mode. */
    uint8_t local_interrupt;              /**< The local APIC input, LINT0 or LINT1. */
} AdvancedConfigurationAndPowerInterfaceProcessorNonMaskableInterrupt_t;

typedef struct {
    uint8_t bus;
    uint8_t source_irq;
    uint32_t global_system_interrupt;
    uint16_t flags;
} AdvancedConfigurationAndPowerInterfaceInterruptSourceOverrideInfo_t;

/**
 * @brief What one reading of a MADT found.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMadtState {
    const char *state_name;                /**< How the reading went, as the boot reports it. */
    bool available;                        /**< Whether every entry was read, to the table's end. */
    bool checksum_valid;                   /**< Whether its bytes sum to zero; one that fails is read all the same. */
    uint32_t local_apic_physical_base;     /**< The local APICs' base, the 64-bit override's when it gives one. */
    uint32_t processor_count;              /**< CPU slots, in the order the table lists them. */
    uint32_t processors_dropped;           /**< Enabled processors past the last slot, counted. */
    uint32_t online_capable_count;         /**< Processors that can be enabled at run time: counted, unused. */
    uint32_t disabled_count;               /**< Processor entries with both flags clear, which OSPM ignores. */
    uint32_t invalid_count;                /**< Enabled entries whose APIC ID names no processor: 0xFF for a local APIC,
                                                0xFFFFFFFF for an x2APIC, or an x2APIC ID below 0xFF when a local APIC
                                                entry whose ID is not 0xFF is Enabled or Online Capable. */
    uint32_t non_maskable_interrupt_count; /**< NMI connections kept. */
    uint32_t non_maskable_interrupts_dropped; /**< NMI connections past the capacity, counted. */
    uint32_t skipped_count;                   /**< Entries of a type not read, skipped by their length. */
    uint32_t too_short_count;  /**< Entries of a type read but shorter than that type: a processor, an I/O APIC,
                                    an override, an NMI connection or the local APIC address lost. */
    uint8_t io_apic_count;     /**< I/O APICs kept. */
    uint32_t io_apics_dropped; /**< I/O APICs past the capacity, counted. */
    uint8_t interrupt_source_override_count;     /**< Interrupt source overrides kept. */
    uint32_t interrupt_source_overrides_dropped; /**< Interrupt source overrides past the capacity, counted. */
    AdvancedConfigurationAndPowerInterfaceProcessor_t
        processors[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_PROCESSOR_COUNT]; /**< The CPU slots. */
    AdvancedConfigurationAndPowerInterfaceProcessorNonMaskableInterrupt_t non_maskable_interrupts
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_NON_MASKABLE_INTERRUPT_COUNT]; /**< The NMI connections. */
    AdvancedConfigurationAndPowerInterfaceIoApicInfo_t
        io_apics[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_IOAPIC_COUNT]; /**< The I/O APICs. */
    AdvancedConfigurationAndPowerInterfaceInterruptSourceOverrideInfo_t
        interrupt_source_overrides[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_ISO_COUNT]; /**< The overrides. */
} AdvancedConfigurationAndPowerInterfaceMadtState_t;

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
 * @brief Tells whether @p length bytes sum to zero, as every ACPI checksum makes them.
 *
 * @param bytes  The first byte.
 * @param length How many bytes the checksum covers.
 * @return true when they sum to zero; false when they do not, or when @p bytes is NULL or
 *         @p length is 0.
 */
extern bool advanced_configuration_and_power_interface_checksum_holds(const void *bytes, size_t length);

/**
 * @brief Whether an I/O port is one the kernel drives itself, which the ACPI layer never reaches
 *        (§5.3): the two 8259s, the ELCR and PCI configuration mechanism #1.
 *
 * @param port The port.
 * @return true when @p port lies in one of those ranges.
 */
extern bool advanced_configuration_and_power_interface_port_is_protected(uint16_t port);

/**
 * @brief Reads the firmware's MADT into the boot's MADT state.
 *
 * @details The state names the MADT as not found when the directory has none, and otherwise holds
 *          what advanced_configuration_and_power_interface_read_madt found in it.
 */
extern void advanced_configuration_and_power_interface_madt_initialize(void);

/**
 * @brief Reads every entry of a MADT.
 *
 * @details Types 0, 1, 2, 4, 5, 9 and 0xA are read. A CPU slot exists only for a local APIC or local
 *          x2APIC entry whose Enabled flag is set; an Online Capable entry is counted and unused, and
 *          one with both flags clear is counted and ignored. A local APIC ID of 0xFF and an x2APIC ID
 *          of 0xFFFFFFFF name no processor, nor does an x2APIC ID below 0xFF when a local APIC entry
 *          whose ID is not 0xFF is Enabled or Online Capable, as Linux does on firmware of ACPI 6.3 or
 *          later; on older firmware Linux also counts a local APIC entry with both flags clear, which
 *          ACPI 6.6 tells OSPM to ignore. Such an entry is counted invalid only when Enabled. An NMI entry for
 * processor UID 0xFF keeps ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ALL_PROCESSORS. Every other type is skipped by
 *          its length and counted; an entry shorter than its type is skipped and counted apart. A
 *          table whose checksum fails is read all the same, and recorded.
 *
 * @param table     The table, every byte of its length readable.
 * @param out_state Emptied, then filled with what the table holds.
 * @return false when @p table is NULL or shorter than a MADT header, when an entry's length runs past
 *         the table's end or is shorter than two bytes, or when the local APIC override lies above
 *         4 GiB; @p out_state then says why, and keeps what was read before.
 */
extern bool
advanced_configuration_and_power_interface_read_madt(const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *table,
                                                     AdvancedConfigurationAndPowerInterfaceMadtState_t *out_state);

/**
 * @brief The boot's MADT state, which advanced_configuration_and_power_interface_madt_initialize fills.
 *
 * @return The state, never NULL.
 */
extern const AdvancedConfigurationAndPowerInterfaceMadtState_t *advanced_configuration_and_power_interface_madt(void);

/**
 * @brief Normalises a FADT (§3.9), as the directory reads it.
 *
 * @details The revision is the earliest one whose FADT is as long: 116 bytes or fewer is revision
 *          1, 132 is 2, 244 is 3, 268 is 5, and longer is 6, whatever the header says. The fields
 *          are read from a zeroed copy of at most
 *          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_FIXED_DESCRIPTION_LENGTH bytes. An extended
 *          register block wins when its address is not 0, its width taken from the legacy length
 *          when it gives none; otherwise a legacy block that is not 0 becomes one in the I/O space,
 *          as wide as its length, capped at 255 bits. PSTATE_CNT and CST_CNT, which ACPI 1.0
 *          reserved, read 0 up to revision 2. The reset register and its value read 0 unless the
 *          FADT is of revision 2 or more and sets RESET_REG_SUP. A hardware-reduced FADT reads 0 at
 *          offsets 46 to 108 and 148 to 243, and in flag bits 1, 2, 3, 7, 8, 13, 14, 16 and 17, which
 *          ACPI tells OSPM to ignore on such a platform.
 *
 * @param fadt            The FADT, every byte of its length readable, or NULL.
 * @param out_description Emptied, then filled with the FADT's fields.
 * @return false when @p fadt is NULL or shorter than a table header; @p out_description is then
 *         not present.
 */
extern bool advanced_configuration_and_power_interface_read_fixed_description(
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *fadt,
    AdvancedConfigurationAndPowerInterfaceFixedDescription_t *out_description);

/**
 * @brief The first I/O port of a register, when the register lies wholly in the I/O space.
 *
 * @param reg   The register.
 * @param bytes How many bytes from its address an access reaches.
 * @return Its port, or 0 when it gives no address, lies in another space, or reaches past port
 *         0xFFFF.
 */
extern uint16_t advanced_configuration_and_power_interface_input_output_port(
    const AdvancedConfigurationAndPowerInterfaceGenericAddress_t *reg, uint32_t bytes);

/**
 * @brief Puts the platform a normalised FADT describes in ACPI mode, through @p host (§3.8 step 0).
 *
 * @details Nothing is written when the platform is hardware-reduced, when SCI_EN, in either PM1
 *          control register, is already set, when SMI_CMD or ACPI_ENABLE is 0, or when a PM1 control
 *          block, SMI_CMD or a PM1 event or GPE block is not wholly in the I/O space or reaches a
 *          protected port. A block too short to hold an enable register is absent. Otherwise every
 *          GPE enable register is written 0, the fixed-event enable bits of each PM1 enable register
 *          are cleared, the other bits written back, and then ACPI_ENABLE is written to SMI_CMD. SCI_EN
 *          is then read up to ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_MAXIMUM_POLLS times, the
 *          host sleeping 1 ms between two reads, until it reads set or the host has waited
 *          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_MAXIMUM_MILLISECONDS. The SCI line is
 *          neither masked nor unmasked here: the caller keeps it masked.
 *
 * @param host       The host whose I/O ports and clock are used.
 * @param fadt       The platform's normalised FADT.
 * @param out_switch Emptied, then filled with the outcome and the counts.
 */
extern void advanced_configuration_and_power_interface_enter_mode(
    const AdvancedConfigurationAndPowerInterfaceHost_t *host,
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt,
    AdvancedConfigurationAndPowerInterfaceModeSwitch_t *out_switch);

/**
 * @brief Puts this machine in ACPI mode, through advanced_configuration_and_power_interface_machine_host
 *        and the FADT of the boot's directory, before any AML runs; needs the kernel tick running.
 */
extern void advanced_configuration_and_power_interface_mode_initialize(void);

/**
 * @brief What advanced_configuration_and_power_interface_mode_initialize did.
 *
 * @return The boot's switch, never NULL; its outcome is
 *         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NOT_RUN before the boot's switch.
 */
extern const AdvancedConfigurationAndPowerInterfaceModeSwitch_t *advanced_configuration_and_power_interface_mode(void);

/**
 * @brief The name the boot's telemetry gives a switch's outcome.
 *
 * @param outcome An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_* outcome.
 * @return Its name, such as "switched", or "unknown" for a value that names none.
 */
extern const char *advanced_configuration_and_power_interface_mode_outcome_name(uint8_t outcome);

/**
 * @brief Walks the tables the root system description pointer of a host leads to, each mapped
 *        through a firmware window of that host.
 *
 * @details The XSDT is walked when the pointer's revision is 2 or more and it names one below 4 GiB
 *          whose signature and checksum hold; the RSDT otherwise, which is also what an XSDT that
 *          fails its checksum falls back to. An XSDT entry above 4 GiB is counted unreachable: this
 *          kernel maps 32-bit physical addresses. The DSDT is then taken from the FADT, whose 64-bit
 *          X_DSDT wins when the FADT is long enough to hold it and it is not 0, as in ACPICA's
 *          AcpiTbSelectAddress, unless it lies above 4 GiB: the 32-bit DSDT is taken then.
 *
 * @param host          The host whose firmware is read.
 * @param out_directory Emptied, then filled with every table found.
 * @return false when the host gives no pointer, one above 4 GiB, which is counted unreachable, or
 *         one that is invalid or names no valid root table.
 */
extern bool advanced_configuration_and_power_interface_read_directory(
    const AdvancedConfigurationAndPowerInterfaceHost_t *host,
    AdvancedConfigurationAndPowerInterfaceDirectory_t *out_directory);

/**
 * @brief Unmaps every table of a directory advanced_configuration_and_power_interface_read_directory
 *        filled, through the host it was read from, and empties it.
 *
 * @param directory The directory, whose table headers must not be used afterwards.
 */
extern void advanced_configuration_and_power_interface_release_directory(
    AdvancedConfigurationAndPowerInterfaceDirectory_t *directory);

/**
 * @brief The directory of this machine's firmware tables, read on the first call through
 *        advanced_configuration_and_power_interface_machine_host.
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

#endif /* KERNEL_ACPI_TABLES_H */
