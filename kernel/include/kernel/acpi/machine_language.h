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
 * A load reads the DSDT and every SSDT in three passes. The declaration pass declares every named
 * object of every table, in order, and skips method bodies by their PkgLength; the Externals pass,
 * which runs only when an External had nowhere to go, places those once every table is declared;
 * the decoding pass decodes every term of every table, method bodies included, so that a call takes
 * as many arguments as the method it names declares. Nothing runs yet: table-level code is decoded and its declarations
 *made, but no predicate is evaluated (docs/acpi/specification.md, §3.6 and §5.1).
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_ACPI_MACHINE_LANGUAGE_H
#define KERNEL_ACPI_MACHINE_LANGUAGE_H

#include <kernel/acpi/machine_language_decoder.h>
#include <kernel/acpi/machine_language_interpreter.h>
#include <kernel/acpi/machine_language_namespace.h>
#include <kernel/acpi/machine_language_objects.h>
#include <kernel/acpi/machine_language_regions.h>
#include <kernel/acpi/machine_language_services.h>
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
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_NOT_RUN                                      \
    3u /**< Block placement: table-level code the declaration pass declares without running it, past a                 \
            predicate that failed. */

/**
 * @brief One definition block of a load, and what decoding it found.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageTable {
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header; /**< Its bytes, header included. */
    uint32_t length;                                                 /**< Its length, header included. */
    bool checksum_valid;                                             /**< Whether its bytes sum to zero. */
    uint32_t terms;                                                  /**< Opcodes the decoding pass decoded. */
    uint32_t decode_errors;                                          /**< Faults the decoding pass met. */
    uint32_t first_error_offset; /**< Offset of the first fault's term, in the table. */
    uint16_t first_error_opcode; /**< The descriptor of the first fault's term, or the invalid one. */
    uint8_t first_error;         /**< The first fault's status. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t;

/**
 * @brief What the decoding pass reports to an observer: each opcode it decodes, and each call, with the
 *        method whose body holds it, 0 at table level.
 *
 * @param context The observer's own state.
 * @param method  The node of the method being decoded, the scope around it when its declaration was not
 *                found, or 0 for table-level code.
 * @param opcode  The descriptor decoded, or ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT
 *                for a call.
 * @param callee  The node a call names, 0 for an opcode.
 */
typedef void (*AdvancedConfigurationAndPowerInterfaceMachineLanguageObserver_t)(void *context, uint32_t method,
                                                                                uint32_t opcode, uint32_t callee);

/**
 * @brief A load: its namespace, its tables, what it counted, and the stacks its passes use.
 *
 * @details The decoding pass decodes a method body as the method would run: the objects it declares
 *          are temporary nodes, created where the declaration names them and removed when the body
 *          ends, so that a name the body declares hides the same name further up while it lasts.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad {
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t space; /**< The namespace it declares into. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t heap;       /**< The objects its nodes hold. */
    uint8_t status;              /**< OK, or why the namespace could not be opened. */
    bool declares_every_branch;  /**< Whether the declaration pass declares every branch of table-level code and
                                      runs none of it, as the census oracle reads AML (§8.4); false at boot. */
    uint8_t integer_bits;        /**< 32 when the DSDT's revision is below 2, 64 otherwise, 0 without a DSDT. */
    uint32_t table_count;        /**< Tables declared. */
    uint32_t bytes;              /**< AML bytes of those tables, headers apart. */
    uint32_t declarations;       /**< Nodes the tables created. */
    uint32_t duplicates;         /**< Declarations of a name that already existed, skipped. */
    uint32_t externals_unplaced; /**< Externals the declaration pass had nowhere to place, which the Externals
                                      pass places once every table is declared. */
    uint32_t load_errors; /**< Faults the declaration and Externals passes met, a name they could not place included. */
    uint8_t first_load_error;     /**< The first of them. */
    uint32_t decode_errors;       /**< Faults the decoding pass met, over every table. */
    uint32_t declared_not_run;    /**< Declarations made in a branch whose predicate failed, without running its
                                       code (§3.6). */
    uint32_t undeclared_faults;   /**< Faults the decoding pass met inside a construct the declaration pass did not
                                       declare: in a branch table-level code did not take, or in a Scope whose
                                       target does not exist. Its names resolve against objects that do not
                                       exist. */
    uint32_t checksum_bad_loaded; /**< Tables whose checksum fails, loaded all the same. */
    uint32_t name_references;     /**< NameStrings the decoding pass read in an operand. */
    uint32_t method_calls;        /**< Of those, the ones that named a method. */
    uint32_t unresolved_names;    /**< Of those, the ones that named nothing. */
    uint32_t pending_peak;        /**< Most operators pending at once. */
    uint32_t blocks_peak;         /**< Most constructs open at once. */
    uint32_t values_deferred;     /**< Declarations whose object needs the interpreter: a Name, a region or a
                                       buffer field built from an expression, and each unit of a BankField whose
                                       BankValue is one. */
    uint32_t table_level_code;    /**< Statements at table level that are neither declarations nor data. */
    uint32_t
        opcodes[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT]; /**< Opcodes the decoding
                                                                                              pass decoded, per
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
    AdvancedConfigurationAndPowerInterfaceMachineLanguageObserver_t
        observer;           /**< Told of each opcode and call the decoding pass decodes, or NULL. */
    void *observer_context; /**< What the observer is given back. */
    uint32_t frame_methods[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FRAMES]; /**< The node of
                                                                                                        each method
                                                                                                        body being
                                                                                                        decoded. */
    uint32_t temporaries[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TEMPORARY_NODES]; /**< The
                                                                                                              temporary
                                                                                                              nodes, in
                                                                                                              creation
                                                                                                              order. */
    uint16_t temporary_bases[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FRAMES]; /**< Temporary
                                                                                                          nodes alive
                                                                                                          when each
                                                                                                          body began. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t evaluation; /**< What running AML holds and
                                                                                       counted. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t regions;       /**< The mapping cache, and what field
                                                                                       accesses counted. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageServices_t services;     /**< What the operating system
                                                                                       services hold and counted. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t;

/**
 * @brief Runs one term of a table's code at load, in that table's frame, whose locals last from one
 *        call to the next: a statement, or the predicate of an If or a While.
 *
 * @details The declaration pass calls it for each statement of table-level code and each predicate
 *          it meets (§3.6); a failure is counted in load->evaluation, never as a load error.
 * @param load        The load, its stacks free.
 * @param table       The table's index in the load.
 * @param scope       The node names are resolved from.
 * @param start       Where the term starts.
 * @param limit       The first offset it may not reach.
 * @param value       Whether the term is a predicate, whose integer is wanted.
 * @param out_integer Receives the predicate's integer when @p value holds and the run succeeds.
 * @param out_after   Receives where the run stopped: past the term when it succeeds.
 * @return OK, or why the run failed: every frame it pushed is gone by then, and every temporary
 *         node it created removed.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_run_table_term(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table, uint32_t scope, uint32_t start,
    uint32_t limit, bool value, uint64_t *out_integer, uint32_t *out_after);

/**
 * @brief Reads a field unit (§5.3): one access per datum of its access width, in its region for a
 *        Field, through its bank or index register for a BankField or an IndexField.
 *
 * @param load      The load.
 * @param node      The unit's node.
 * @param out_value Receives an Integer when the unit is no wider than the load's integers, a Buffer
 *                  of its bytes otherwise.
 * @return OK; TYPE_MISMATCH when the node holds no unit or a register is not a unit of a Field;
 *         GLOBAL_LOCK_UNAVAILABLE when a Lock field cannot take the global lock, the firmware never
 *         releasing it; REGION_UNEVALUATED, REGION_BOUNDS,
 *         ADDRESS_UNREACHABLE or SPACE_UNSUPPORTED when a datum cannot be reached; ACCESS_WIDTH_INVALID
 *         for a reserved AccessType or a datum wider than its space; INDEX_OUT_OF_RANGE for an index
 *         or a BankValue its register cannot hold; LIMIT_FIELD_ACCESSES once the evaluation's field
 *         accesses are spent; LIMIT_BUFFER_BYTES or OUT_OF_MEMORY when its Buffer cannot be built.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_field_read(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value);

/**
 * @brief Writes a value to a field unit (§5.3): an Integer's bits, cut or zero-extended to the unit,
 *        or a String's or a Buffer's bytes in chunks of the unit's size rounded up to whole bytes, one
 *        after the other, the last one zero-extended and an empty value writing nothing; a datum the
 *        unit covers in part keeps its other bits by the unit's UpdateRule.
 *
 * @param load  The load.
 * @param node  The unit's node.
 * @param value What to write.
 * @return OK, or why it failed, as for the read; TYPE_MISMATCH too for a value of another type, and
 *         LIMIT_BUFFER_BYTES for a unit wider than the largest Buffer. A failure may leave the datums
 *         before it written.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_field_write(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value);

/**
 * @brief Takes the attach of a PCI_Config region as far as it goes without evaluating a method
 *        (§4.4): the root bridge above it, whose _HID or _CID is PNP0A03 or PNP0A08, its _SEG and
 *        _BBN, then the _ADR of each device on the way down to the region, the bridges among them
 *        moving the bus to their secondary one. A method among those objects is named for the caller
 *        to evaluate in an internal frame (§3.6) and hand back.
 *
 * @param load       The load.
 * @param region     The region's node.
 * @param result     What the method the previous call named returned, or NULL.
 * @param out_method The method, without arguments, whose result the attach waits for, or 0 once the
 *                   region is attached.
 * @return OK; TYPE_MISMATCH when @p region holds no PCI_Config region.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_attach(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t region,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *result, uint32_t *out_method);

/**
 * @brief The first PCI_Config region a unit's accesses reach that is not attached yet: its own, or
 *        those of the registers of an IndexField or a BankField.
 *
 * @param load The load.
 * @param unit The unit's node.
 * @return The region's node, or 0.
 */
extern uint32_t
advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_unattached_region(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t unit);

/**
 * @brief One access of 1, 2 or 4 bytes to the configuration space of a PCI_Config region's function,
 *        attached first when that takes no method: a write to its protected header is dropped, a
 *        function the attach could not reach reads all ones and drops writes, and an offset its width
 *        does not divide is made byte by byte, all counted (§5.3).
 *
 * @param load   The load.
 * @param region The region's node.
 * @param offset The offset in the configuration space.
 * @param width  1, 2 or 4.
 * @param write  Whether the access writes @p value, rather than reading into it.
 * @param value  The bytes written, or read.
 * @return OK; REGION_UNEVALUATED when the attach needs a method, ADDRESS_UNREACHABLE past offset 0xFF
 *         or in a segment other than 0, which need ECAM (#58).
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_access(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t region, uint64_t offset, uint8_t width,
    bool write, uint32_t *value);

/**
 * @brief The _REG a PCI_Config region is registered through (§5.3): the method named _REG in the
 *        region's own scope, never one above it, whatever arguments it takes.
 *
 * @param load        The load.
 * @param region_node The region's node.
 * @return The method's node; 0 when the node holds no PCI_Config region or its scope no such method.
 */
extern uint32_t
advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_registration(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t region_node);

/**
 * @brief Says that the initialisation has run the _REG of every scope that declares a PCI_Config
 *        region (§3.8 step 3): from now on, a PCI_Config region a method creates gets its _REG at once.
 *
 * @param load The load.
 */
extern void advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_registered(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Gives a node the OperationRegion object an OperationRegion in a method creates (§5.3); a
 *        table's regions are built in place by its load, which keeps window W1 within its budget.
 *
 * @param load   The load.
 * @param node   The region's node.
 * @param space  Its RegionSpace byte.
 * @param offset Its offset in its space.
 * @param length Its length in bytes.
 * @return OK; OUT_OF_MEMORY or LIMIT_OBJECT_RUNS when the heap cannot hold the object.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_region_build(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node, uint8_t space, uint64_t offset,
    uint64_t length);

/**
 * @brief Gives a node the field unit object @p shape describes (§5.3), whether a table declares it or
 *        a method creates it.
 *
 * @param load  The load.
 * @param node  The unit's node.
 * @param shape Its kind, its registers, its access, update and lock rules, its first bit and length.
 * @return OK; OUT_OF_MEMORY or LIMIT_OBJECT_RUNS when the heap cannot hold the object.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_field_unit_build(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *shape);

/**
 * @brief Gives the load the global lock of its platform, before any table-level code runs: the
 *        GlobalLock word of the FACS at @p firmware_control, when the FACS is not usable memory, has
 *        its signature and is long enough; none otherwise, and a Lock field is then accessed without
 *        a lock, counted.
 *
 * @param load             The load.
 * @param firmware_control The FACS, or 0 for a platform without a global lock, hardware-reduced ones
 *                         included.
 * @param control_ports    The PM1a and PM1b control ports GBL_RLS is written to, each 0 when absent.
 */
extern void advanced_configuration_and_power_interface_machine_language_use_global_lock(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t firmware_control,
    const uint16_t *control_ports);

/**
 * @brief Takes the global lock for `Acquire (\_GL)` as a Lock field takes it (§4.4, §5.4): owned
 *        when free, pending while the firmware owns it, then a bounded wait; an acquisition already
 *        holding it nests, and a platform without a global lock takes nothing, counted.
 *
 * @param load The load.
 * @return OK; GLOBAL_LOCK_UNAVAILABLE when the firmware never releases it; LIMIT_FIELD_ACCESSES once
 *         the evaluation's field accesses are spent by the reads of the lock word.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_take_global_lock(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Gives back the global lock the last acquisition holding it took, signalling the firmware
 *        when it asked for the lock meanwhile.
 *
 * @param load The load.
 */
extern void advanced_configuration_and_power_interface_machine_language_release_global_lock(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Answers `\_OSI` as Windows does (§5.4): True for every Windows string from "Windows 2000"
 *        to "Windows 2022", False for any other, "Linux", the vendor strings and every feature
 *        string included. The newest Windows string asked is recorded, and protects more ports from
 *        "Windows 2001" on (§5.3).
 *
 * @param load          The load.
 * @param argument      The method's argument.
 * @param out_supported Receives whether the string names an interface the kernel answers True.
 * @return OK; TYPE_MISMATCH when the argument is no String.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_operating_system_interface(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *argument, bool *out_supported);

/**
 * @brief Whether AML may not reach an I/O port (§5.3): always the 8259s, the ELCR and PCI mechanism
 *        #1; once \_OSI was asked for Windows XP or newer, also the DMA, PIT, RTC and CMOS, page,
 *        arbitration and board ports Windows' ACPI driver protects.
 *
 * @param load The load.
 * @param port The first port of the access.
 * @return true when the access is to be refused: a read gives 0, a write is dropped.
 */
extern bool advanced_configuration_and_power_interface_machine_language_port_is_protected(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint16_t port);

/**
 * @brief Starts an evaluation, a table's load or one the host asked for: its budgets of §6.1, While
 *        back-edges, field accesses and requested time, start from zero.
 *
 * @param load The load.
 */
extern void advanced_configuration_and_power_interface_machine_language_begin_evaluation(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Ends an evaluation: every mutex it still holds is released, `\_GL`'s global lock with it,
 *        counted, and the sync level drops to 0 (§5.2).
 *
 * @param load The load.
 */
extern void advanced_configuration_and_power_interface_machine_language_end_evaluation(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Runs Sleep (§5.5): the milliseconds are charged to the evaluation's budget first, then the
 *        host gives up the processor that long; Sleep (0) waits nothing.
 *
 * @param load         The load.
 * @param milliseconds What AML asked for.
 * @return OK; LIMIT_TIME_REQUESTED, before any wait, when the request would take the evaluation past
 *         30 s.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_sleep(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint64_t milliseconds);

/**
 * @brief Runs Stall (§5.5): the microseconds are charged to the evaluation's budget first; up to
 *        255 µs the host busy-waits, beyond it sleeps the milliseconds they round up to; both are
 *        counted beyond 100 µs.
 *
 * @param load         The load.
 * @param microseconds What AML asked for.
 * @return OK; LIMIT_TIME_REQUESTED, before any wait, when the request would take the evaluation past
 *         30 s.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_stall(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint64_t microseconds);

/**
 * @brief Runs Timer (§5.5): the host's monotonic clock, in units of 100 ns.
 *
 * @param load The load.
 * @return The clock, masked to the load's integer width.
 */
extern uint64_t advanced_configuration_and_power_interface_machine_language_timer(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Queues a Notify, delivered once the evaluation that raised it is over (§3.7); a full ring
 *        drops it, counted.
 *
 * @param load  The load.
 * @param node  The object notified.
 * @param value The notification value.
 * @return OK; TYPE_MISMATCH when the object is no device, processor or thermal zone.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_notify(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node, uint64_t value);

/**
 * @brief Takes the oldest Notify from the ring, counted as taken.
 *
 * @param load             The load.
 * @param out_notification Receives it.
 * @return false when the ring is empty.
 */
extern bool advanced_configuration_and_power_interface_machine_language_take_notification(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNotification_t *out_notification);

/**
 * @brief Takes every Notify from the ring and acts on none, each counted as taken: what the boot
 *        does with the Notifies its last evaluations raised, once step 9 evaluated each _CST a Notify
 *        0x81 changed (§3.8 step 9), until #547 acts on them.
 *
 * @param load The load.
 */
extern void advanced_configuration_and_power_interface_machine_language_drop_notifications(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Runs Acquire (§5.2): the mutex is taken, or nested when the evaluation holds it; one whose
 *        SyncLevel is below the current level is ignored, counted, and not held, as uACPI ignores it.
 *        `\_GL` takes the global lock too.
 *
 * @param load         The load.
 * @param node         The object named.
 * @param out_acquired Receives whether the mutex is held: Acquire gives 0 then, Ones otherwise.
 * @return OK; TYPE_MISMATCH when the object is no mutex; LIMIT_HELD_MUTEXES when the evaluation holds
 *         as many as §6.1 allows; why the global lock could not be taken.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_acquire(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node, bool *out_acquired);

/**
 * @brief Runs Release (§5.2): one acquisition is given back, out of order when the mutex has the
 *        SyncLevel of the one acquired last; a mutex not held, or of another level, is ignored,
 *        counted, as uACPI ignores it.
 *
 * @param load The load.
 * @param node The object named.
 * @return OK; TYPE_MISMATCH when the object is no mutex.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_release(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node);

/**
 * @brief Runs Signal (§5.2): the Event's count of pending signals goes up by one, saturating.
 *
 * @param load The load.
 * @param node The object named.
 * @return OK; TYPE_MISMATCH when the object is no Event.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_signal(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node);

/**
 * @brief Enters a method whose MethodFlags are @p flags: a Serialized one raises the sync level to
 *        its SyncLevel, and is refused below the current level (§5.2).
 *
 * @param load      The load.
 * @param flags     The method's MethodFlags.
 * @param out_saved Receives what advanced_configuration_and_power_interface_machine_language_leave_method
 *                  restores: 0 for a method that is not Serialized.
 * @return OK; MUTEX_ORDER when the method's SyncLevel is below the current level.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_enter_method(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint8_t flags, uint8_t *out_saved);

/**
 * @brief Leaves a method: a Serialized one gives back the sync level it found.
 *
 * @param load  The load.
 * @param saved What advanced_configuration_and_power_interface_machine_language_enter_method gave.
 */
extern void advanced_configuration_and_power_interface_machine_language_leave_method(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint8_t saved);

/**
 * @brief Gives every mapping of the load's cache back to its host.
 *
 * @param load The load.
 */
extern void advanced_configuration_and_power_interface_machine_language_regions_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Counts a While back-edge of table-level code against the budget of 2^20 that a table's code
 *        shares with the methods it calls (§6.1): a table load is one evaluation.
 *
 * @param load The load.
 * @return OK, or LIMIT_LOOP_ITERATIONS once the table's code has taken more back-edges than §6.1 allows.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_count_back_edge(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Evaluates the object an absolute path names (§3.6): a method runs with the arguments given,
 *        a named object gives its value.
 *
 * @details The load's stacks must be free. The arguments are shared with the method, not copied
 *          (§5.2); their references stay the caller's.
 * @param load           A load whose tables are decoded.
 * @param path           A path from the root, such as `\_SB_.PCI0._STA`, its NameSegs padded with `_` or
 *                       not.
 * @param arguments      The method's arguments, or NULL when it takes none.
 * @param argument_count How many: exactly as many as the method takes (§5.2).
 * @param out_result     Receives the result, which holds a reference of its own: the caller gives it
 *                       back with advanced_configuration_and_power_interface_machine_language_value_release.
 * @return TRUNCATED for a path that does not start at the root or is malformed; NAME_NOT_FOUND for one
 *         that names nothing; TYPE_MISMATCH for the wrong number of arguments or an object that is no
 *         data; why the method failed otherwise, every frame it pushed gone by then; OK on success.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_evaluate(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, const char *path,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *arguments, uint32_t argument_count,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_result);

/**
 * @brief Evaluates the object a node holds, as
 *        advanced_configuration_and_power_interface_machine_language_evaluate evaluates the one a path
 *        names.
 *
 * @param load           A load whose tables are decoded, its stacks free.
 * @param node           The node, which holds the object.
 * @param arguments      The method's arguments, or NULL when it takes none.
 * @param argument_count How many: exactly as many as the method takes (§5.2).
 * @param out_result     Receives the result, which the caller gives back with
 *                       advanced_configuration_and_power_interface_machine_language_value_release.
 * @return TYPE_MISMATCH for the wrong number of arguments or an object that is no data; why the method
 *         failed otherwise, every frame it pushed gone by then; OK on success.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_evaluate_node(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *arguments, uint32_t argument_count,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_result);

/**
 * @brief Reads one element of a Package, a name it holds resolved as §5.1 resolves it when read: the
 *        object the name names, a field unit's read, never a method's evaluation.
 *
 * @param load      The load.
 * @param package   A Package value.
 * @param element   The element, counted from 0.
 * @param out_value Receives the element, which the caller gives back with
 *                  advanced_configuration_and_power_interface_machine_language_value_release.
 * @return TYPE_MISMATCH when @p package is no Package, the element is Uninitialized or names a method;
 *         INDEX_OUT_OF_RANGE past its last element; why its name could not be resolved or read; OK
 *         otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_package_element(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *package, uint32_t element,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value);

/**
 * @brief Releases what Local0 to Local7 of table-level code hold, once a table's code has run: its
 *        locals do not outlive the table.
 *
 * @param load The load.
 */
extern void advanced_configuration_and_power_interface_machine_language_release_table_locals(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Counts a failure of table-level code: in load->evaluation, per status, the first one kept.
 *
 * @param load   The load.
 * @param status Why it failed, not OK.
 */
extern void advanced_configuration_and_power_interface_machine_language_count_failure(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status);

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
 * @brief Runs the declaration pass over one definition block: every named object it declares is created,
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
 * @brief Places the Externals the declaration pass could not, in a pass over every table declared when
 *        there is one, then runs the decoding pass: every term is decoded and counted, method bodies
 *        included, and each fault is recorded in its table and skipped as the declaration pass skips
 *        it.
 *
 * @details An External whose path does not exist once every table is declared is placed all the
 *          same, its missing segments created as Externals with no object: firmware calls such a
 *          method behind CondRefOf, and the decoding pass needs its argument count.
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
 * @brief Folds a load's namespace as the parity record of the specification (§8.7) does: FNV-1a,
 *        32 bits, over a depth-first walk in declaration order, External nodes apart. Per node: its
 *        depth, its name and its type, then an Integer; a String's or a Buffer's length and bytes; a
 *        Package's count and each element the same way, a name element as its path bytes; a
 *        method's flags and body length; a region's space, offset and length; a field unit's kind,
 *        bit offset, bit length, access and update rule; a Processor's ID, block address and block
 *        length. Nothing that holds an address or an index is folded.
 *
 * @param load A load whose tables are decoded.
 * @return The fold.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_namespace_fold(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Gives a load's pages back to its host.
 *
 * @param load The load; its nodes must not be used afterwards.
 */
extern void advanced_configuration_and_power_interface_machine_language_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_H */
