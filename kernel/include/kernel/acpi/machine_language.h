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
 * @file machine_language.h
 * @brief The AML interpreter's interface: the load of the definition blocks and what it counted.
 *
 * A load reads the DSDT and every SSDT in two passes. The first declares every named object of
 * every table, in order, and skips method bodies by their PkgLength; the second decodes every term
 * of every table, method bodies included, so that a call takes as many arguments as the method it
 * names declares. Nothing runs yet: table-level code is decoded and its declarations made, but no
 * predicate is evaluated (docs/acpi/specification.md, §3.6 and §5.1).
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_ACPI_MACHINE_LANGUAGE_H
#define KERNEL_ACPI_MACHINE_LANGUAGE_H

#include <kernel/acpi/machine_language_decoder.h>
#include <kernel/acpi/machine_language_namespace.h>
#include <kernel/acpi/tables.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES                                         \
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_TABLE_COUNT /**< Definition blocks one load holds. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_PENDING_OPERATORS                              \
    256u /**< Operators waiting for their operands (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BLOCKS                                         \
    512u /**< Constructs open at once (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TEMPORARY_NODES                                \
    1024u /**< Nodes the methods being decoded create (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FRAMES                                         \
    64u /**< Method bodies nested in one another (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_IN_METHOD                                    \
    1u /**< Block placement: inside a method body. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_METHOD_BODY                                  \
    2u /**< Block placement: a method body itself, whose end removes the nodes it created. */

/**
 * @brief An operator whose operands are being decoded (§3.4).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator {
    uint16_t opcode; /**< Its descriptor index; ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT
                          for a method call. */
    uint8_t operands_done; /**< Operands decoded or started. */
    uint8_t operand_count; /**< Operands it takes: its descriptor's, or the called method's arguments. */
    uint32_t start;        /**< Offset of its opcode or name in its table. */
    uint32_t end;          /**< Its PkgLength end, or the limit it inherited. */
    uint32_t node;         /**< The node its NameString declared or named, or 0. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator_t;

/**
 * @brief A construct whose terms are being decoded (§3.4).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageBlock {
    uint8_t kind;          /**< What it holds: an ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_*
                                list kind, NONE for a whole table. */
    uint8_t placement;     /**< 0 at table level, else
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_IN_METHOD     or
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_METHOD_BODY. */
    uint16_t pending_base; /**< Pending operators below it, which belong to the constructs around it. */
    uint32_t end;          /**< Its end. */
    uint32_t scope;        /**< The scope its names are read in. */
    uint32_t start;        /**< Offset of the opcode that opened it, or of the table's first term. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageBlock_t;

/**
 * @brief One definition block of a load, and what decoding it found.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageTable {
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header; /**< Its bytes, header included. */
    uint32_t length;                                                 /**< Its length, header included. */
    bool checksum_valid;                                             /**< Whether its bytes sum to zero. */
    uint32_t terms;                                                  /**< Opcodes the second pass decoded. */
    uint32_t decode_errors;                                          /**< Faults the second pass met. */
    uint32_t first_error_offset; /**< Offset of the first fault's term, in the table. */
    uint16_t first_error_opcode; /**< The descriptor of the first fault's term, or the invalid one. */
    uint8_t first_error;         /**< The first fault's status. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t;

/**
 * @brief A load: its namespace, its tables, what it counted, and the stacks its passes use.
 *
 * @details The second pass decodes a method body as the method would run: the objects it declares
 *          are temporary nodes, created where the declaration names them and removed when the body
 *          ends, so that a name the body declares hides the same name further up while it lasts.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad {
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t space; /**< The namespace it declares into. */
    uint8_t status;               /**< OK, or why the namespace could not be opened. */
    uint8_t integer_bits;         /**< 32 when the DSDT's revision is below 2, 64 otherwise, 0 without a DSDT. */
    uint32_t table_count;         /**< Tables declared. */
    uint32_t bytes;               /**< AML bytes of those tables, headers apart. */
    uint32_t declarations;        /**< Nodes the tables created. */
    uint32_t duplicates;          /**< Declarations of a name that already existed, skipped. */
    uint32_t load_errors;         /**< Faults the first pass met, a name it could not place included. */
    uint8_t first_load_error;     /**< The first of them. */
    uint32_t decode_errors;       /**< Faults the second pass met, over every table. */
    uint32_t checksum_bad_loaded; /**< Tables whose checksum fails, loaded all the same. */
    uint32_t name_references;     /**< NameStrings the second pass read in an operand. */
    uint32_t method_calls;        /**< Of those, the ones that named a method. */
    uint32_t unresolved_names;    /**< Of those, the ones that named nothing. */
    uint32_t pending_peak;        /**< Most operators pending at once. */
    uint32_t blocks_peak;         /**< Most constructs open at once. */
    uint32_t opcodes[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT]; /**< Opcodes the
                                                                                                    second pass
                                                                                                    decoded, per
                                                                                                    descriptor. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t
        tables[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES]; /**< In load order. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator_t
        pending[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_PENDING_OPERATORS]; /**< The operator
                                                                                                        stack. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageBlock_t
        blocks[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BLOCKS]; /**< The block stack. */
    uint32_t temporary_count;                                                           /**< Temporary nodes alive. */
    uint32_t frame_count; /**< Method bodies being decoded. */
    uint32_t temporaries[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TEMPORARY_NODES]; /**< The
                                                                                                              temporary
                                                                                                              nodes, in
                                                                                                              creation
                                                                                                              order. */
    uint16_t temporary_bases[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FRAMES]; /**< Temporary
                                                                                                          nodes alive
                                                                                                          when each
                                                                                                          body began. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t;

/**
 * @brief Opens a load: an empty namespace with its predefined objects, and no table.
 *
 * @param load            Filled; closed with advanced_configuration_and_power_interface_machine_language_close,
 *                        even when this fails.
 * @param host            The host its namespace takes pages from.
 * @param max_nodes       The node cap.
 * @param max_arena_bytes The arena cap.
 * @return false when the namespace could not be opened; load->status says why.
 */
extern bool advanced_configuration_and_power_interface_machine_language_open(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint32_t max_nodes, uint32_t max_arena_bytes);

/**
 * @brief Runs the first pass over one definition block: every named object it declares is created,
 *        method bodies are skipped.
 *
 * @details A table whose checksum fails is declared all the same, and counted. A declaration whose
 *          path does not exist is counted in load_errors and skipped alone, its body with it; an
 *          External is left to a later pass instead, since it may name what a later table declares.
 *          Any other fault skips the innermost construct with a PkgLength around it, or the rest of
 *          the table when there is none or when a PkgLength crosses its parent, and is counted too.
 * @param load   An open load.
 * @param header The table, every byte of its length readable; kept until the load is closed.
 * @return false when the load already holds ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES
 *         tables, or the table is shorter than its header; nothing is declared then.
 */
extern bool advanced_configuration_and_power_interface_machine_language_declare_table(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header);

/**
 * @brief Places the Externals the first pass could not, in a pass over every table declared, then runs
 *        the second pass: every term is decoded and counted, method bodies included, and each fault
 *        is recorded in its table and skipped as the first pass skips it.
 *
 * @details An External whose path does not exist once every table is declared is placed all the
 *          same, its missing segments created as Externals with no object: firmware calls such a
 *          method behind CondRefOf, and the second pass needs its argument count.
 *
 * @param load An open load whose tables are declared.
 */
extern void advanced_configuration_and_power_interface_machine_language_decode_tables(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Loads the DSDT, then every SSDT, of a directory: both passes over every table.
 *
 * @param load      An open load, with no table yet.
 * @param directory The directory, whose tables stay mapped until the load is closed.
 */
extern void advanced_configuration_and_power_interface_machine_language_load_directory(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory);

/**
 * @brief Gives a load's pages back to its host.
 *
 * @param load The load; its nodes must not be used afterwards.
 */
extern void advanced_configuration_and_power_interface_machine_language_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Loads this machine's definition blocks at boot, into the boot's load, with the caps of the
 *        build's profile.
 */
extern void advanced_configuration_and_power_interface_machine_language_initialize(void);

/**
 * @brief The boot's load, which advanced_configuration_and_power_interface_machine_language_initialize fills.
 *
 * @return The load, never NULL.
 */
extern const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *
advanced_configuration_and_power_interface_machine_language(void);

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_H */
