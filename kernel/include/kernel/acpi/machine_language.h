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
 * A load reads the DSDT and every SSDT in three passes. The declaration pass runs the code of each
 * table, in order, in the interpreter, which hands the load each named object it meets to declare and
 * skips method bodies by their PkgLength; the Externals pass, which runs only when an External had
 * nowhere to go, places those once every table is declared; the decoding pass decodes every term of
 * every table, method bodies included, so that a call takes as many arguments as the method it names
 * declares, unless the load skips bodies, as a release kernel's does (docs/acpi/specification.md, §3.6
 * and §5.1).
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
#include <kernel/acpi/machine_language_predecoding.h>
#include <kernel/acpi/machine_language_regions.h>
#include <kernel/acpi/machine_language_services.h>
#include <kernel/acpi/tables.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES                                         \
    128u /**< Definition blocks one load holds: those of the root table, then those Load and LoadTable add (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLE_BYTES                                    \
    (256u * 1024u) /**< Bytes Load copies, over every table it adds (§6.1). */
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
 * @brief Where a definition block of a load comes from (§3.4).
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageTableOrigin {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_TABLE_STATIC = 0, /**< The root table lists it: it is
                                                                                     declared at boot, in place. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_TABLE_LOAD = 1,   /**< Load copied it from a region
                                                                                     or a field. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_TABLE_LOAD_TABLE = 2, /**< LoadTable found it in the
                                                                                         directory: it runs in place. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageTableOrigin_t;

/**
 * @brief One definition block of a load, and what decoding it found: the loaded-table registry (§3.4).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageTable {
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header; /**< Its bytes, header included: the firmware
                                                                          window of a table read in place, the copy
                                                                          Load made of any other. */
    uint64_t physical_address;                                       /**< Where Load read it: its region's address;
                                                                          0 for a field and a table read in place. */
    uint32_t length;                                                 /**< Its length, header included. */
    bool checksum_valid;                                             /**< Whether its bytes sum to zero. */
    uint8_t origin;              /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_TABLE_* origin. */
    uint16_t pages;              /**< Pages of the namespace arena its copy takes, 0 for a table read in place. */
    uint32_t terms;              /**< Opcodes the decoding pass decoded. */
    uint32_t data_kept;          /**< Data objects of a Name its declaration kept in it (O20). */
    uint32_t decode_errors;      /**< Faults the decoding pass met. */
    uint32_t first_error_offset; /**< Offset of the first fault's term, in the table. */
    uint16_t first_error_opcode; /**< The descriptor of the first fault's term, or the invalid one. */
    uint8_t first_error;         /**< The first fault's status. */
    bool load_failed;            /**< Whether its load met a fault: a fault of a pass that declares it, or a failure
                                      of its table-level code (§3.8 step 1). */
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

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLARATION_ANSWERS                                \
    2u /**< Terms one statement of table-level code waits for at most: an OperationRegion's offset and length. */

/**
 * @brief What the load did with one statement of table-level code the interpreter handed it (§3.6): where
 *        the code goes on, the TermList the interpreter runs next, or the term the statement waits for.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageDeclaration {
    uint32_t after;    /**< Where table-level code goes on: past the statements, or past the TermList they open. */
    uint32_t body;     /**< Where the TermList of the Scope, Device, Processor, PowerResource or ThermalZone the
                            statement declared starts, which the interpreter runs up to after, or 0. */
    uint32_t scope;    /**< The node that TermList runs in. */
    uint32_t asks;     /**< Where the term the statement waits for starts, an OperationRegion's offset or length or
                            a BankField's BankValue that is no constant, or 0 when it waits for none. */
    uint32_t asks_end; /**< The first offset that term may not reach. */
    uint32_t resumes;  /**< Where the statement goes on from once the term is answered. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator_t waiting; /**< The operator that waits. */
    uint64_t values[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLARATION_ANSWERS]; /**< The Integer
                                                                                                          each term
                                                                                                          answered
                                                                                                          gave. */
    uint32_t afters[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLARATION_ANSWERS]; /**< Where each
                                                                                                          of them
                                                                                                          ended. */
    bool known[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLARATION_ANSWERS]; /**< Whether each gave
                                                                                                    one: false when
                                                                                                    it failed. */
    uint32_t answers_for; /**< The offset of the statement the answers belong to, or 0. */
    uint8_t answered;     /**< Terms answered so far, in the order that statement asked for them. */
    bool closed;          /**< Whether the TermList of an If declared without running its code reached its end. */
    bool leaves;          /**< Whether a fault skipped the rest of the construct the statement stands in (§6.2). */
    bool abandons;        /**< Whether a fault abandoned the rest of the table (§6.2). */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageDeclaration_t;

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LIMITS                                             \
    21u /**< Limits of §6.1 a load counts, the record cache's apart. */

/**
 * @brief One limit of §6.1, as a load counted it: the times it was reached, and the most it held at once.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageLimit {
    const char *hits_name; /**< What its hits are counted under: the status a fault reaching it fails with (§6.2), or
                                the counter of what it drops; NULL when reaching it is no fault and drops nothing, as
                                a deeper package waits for the interpreter and a full mapping cache gives its oldest
                                entry back. */
    const char *peak_name; /**< The key its peak is printed under. */
    uint32_t hits;         /**< The faults counted under that status (§4.5), or what that counter holds. */
    uint32_t peak;         /**< The most it held at once, never past its cap. */
    bool comparable;       /**< Whether the peak is the same on every target, which a parity record compares; false
                                for the pages and runs memory takes (§8.7). */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageLimit_t;

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
    uint8_t status;               /**< OK, or why the namespace could not be opened. */
    bool declares_every_branch;   /**< Whether the declaration pass declares every branch of table-level code and
                                       runs none of it, as the census oracle reads AML (§8.4); false at boot. */
    bool skips_bodies;            /**< Whether the load decodes no method body once its tables are declared, nor
                                       those of a table Load adds, as a release kernel loads (O2, §3.6); false in a
                                       debug kernel and in the host tools. */
    bool general_load;            /**< Whether the declaration pass takes none of its shortcuts (§3.6), and a kept
                                       object is built by the general data decoder instead of the builder of kept
                                       data (C4): the differential test compares them with this general path; false
                                       at boot. */
    bool predecodes_loops;        /**< Whether the statements of a method's While that loops run from records
                                       (§3.6): true unless the boot line holds lpl.acpi.predecoding=off. */
    uint8_t integer_bits;         /**< 32 when the DSDT's revision is below 2, 64 otherwise, 0 without a DSDT. */
    uint32_t table_count;         /**< Tables declared. */
    uint32_t tables_loaded;       /**< Of those, the ones Load and LoadTable added. */
    uint32_t load_duplicates;     /**< Load and LoadTable calls refused, False returned, for a table the load holds. */
    uint32_t load_tables_missing; /**< LoadTable calls that found no table, False returned. */
    uint32_t unload_refused;      /**< Unloads refused, their operand evaluated and nothing else done (§4.5). */
    uint32_t table_bytes_copied;  /**< Bytes Load copied. */
    uint32_t declaring;           /**< Passes and table frames that declare: not 0 while a table is declared. */
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory; /**< The directory LoadTable searches, and
                                                                             the one a memory write is checked
                                                                             against (§5.3), or NULL. */
    uint32_t bytes;             /**< AML bytes of those tables, headers apart. */
    uint32_t declarations;      /**< Nodes the tables created. */
    uint32_t data_stepped_over; /**< Data objects of a Name the declaration pass built, then stepped over instead of
                                     decoding them as terms (O18); every load steps over the data it keeps in its
                                     table, counted in data_kept (O20). */
    uint32_t declarations_decoded_directly; /**< Statements the declaration pass took through its own decoder
                                                 instead of operand by operand through the walk (O19). */
    uint32_t integers_built_directly;       /**< Of the data objects stepped over, the integer constants the
                                                 declaration pass's decoder built itself instead of the general data
                                                 decoder (O19). */
    uint32_t scopes_entered;                /**< TermLists of a Scope, Device, Processor, PowerResource or
                                                 ThermalZone the declaration pass entered itself while the interpreter
                                                 runs a table's code, instead of handing each back to it (O21). */
    uint32_t data_kept;           /**< Data objects of a Name the declaration pass kept in their table once their head
                                       read, neither checked nor built, and stepped over, until a name resolution
                                       reaches them (O20). */
    uint32_t data_materialized;   /**< Of those, the ones a name resolution has built since. */
    uint32_t data_faults_read;    /**< Of those, the ones a name resolution found a fault in, left Uninitialized; one
                                       that needs the interpreter is counted in values_deferred instead (O20). */
    uint32_t data_built_directly; /**< Of those, the ones the builder of kept data built (C4), not the general data
                                       decoder, which a load whose general_load is set uses. */
    uint32_t duplicates;          /**< Declarations of a name that already existed, skipped. */
    uint32_t externals_unplaced;  /**< Externals the declaration pass had nowhere to place, which the Externals
                                       pass places once every table is declared. */
    uint32_t load_errors; /**< Faults the declaration and Externals passes met, a name they could not place included. */
    uint8_t first_load_error;   /**< The first of them. */
    uint32_t table_load_failed; /**< Tables the load declares from the directory, the DSDT and the SSDTs of the root
                                     table, whose load met a fault, counted once each, the next table loaded all the
                                     same (§3.8 step 1). */
    uint32_t faults[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT]; /**< The lines
                                                                                                   logged per status
                                                                                                   (§4.5): a debug
                                                                                                   kernel's decoding
                                                                                                   pass adds those it
                                                                                                   meets in method
                                                                                                   bodies. */
    uint32_t decode_errors;         /**< Faults the decoding pass met, over every table. */
    uint32_t data_faults;           /**< Data objects kept in their table (O20) that the check run once the bodies are
                                         decoded found would not build, a fault in their bytes or a term that needs the
                                         interpreter; one code read before that check is counted at that read. */
    uint32_t data_faults_signature; /**< The sum, over the nodes the check counted in data_faults, of
                                         advanced_configuration_and_power_interface_machine_language_kept_data_signature
                                         of each node's index: two loads whose checks flag the same nodes hold the
                                         same sum. */
    uint32_t data_named_sizes;      /**< Data objects kept in their table (O20) that the same check found hold a
                                         Buffer or a VarPackage whose size or count a name gives: their first read
                                         reads that name, where a load that built them read it as it declared them. */
    uint32_t declared_not_run;      /**< Declarations made in a branch whose predicate failed, without running its
                                          code (§3.6). */
    uint32_t undeclared_faults;     /**< Faults the decoding pass met inside a construct the declaration pass did not
                                         declare: in a branch table-level code did not take, or in a Scope whose
                                         target does not exist. Its names resolve against objects that do not
                                         exist. */
    uint32_t checksum_bad_loaded;   /**< Tables whose checksum fails, loaded all the same. */
    uint32_t name_references;       /**< NameStrings the decoding pass read in an operand. */
    uint32_t method_calls;          /**< Of those, the ones that named a method. */
    uint32_t unresolved_names;      /**< Of those, the ones that named nothing. */
    uint32_t pending_peak;          /**< Most operators pending at once. */
    uint32_t blocks_peak;           /**< Most constructs open at once. */
    uint32_t values_deferred;       /**< Declarations whose object needs the interpreter: a Name, a region or a
                                         buffer field built from an expression, each unit of a BankField whose
                                         BankValue is one, and each data object kept in its table (O20) whose first
                                         read met such a term. */
    uint32_t table_level_code;      /**< Statements at table level that are neither declarations nor data. */
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
    AdvancedConfigurationAndPowerInterfaceMachineLanguageDeclaration_t
        declaration;               /**< The last statement of table-level code the interpreter handed the load. */
    uint32_t temporary_count;      /**< Temporary nodes alive. */
    uint32_t temporary_nodes_peak; /**< Most of them alive at once, in an evaluation or in the decoding pass. */
    uint32_t frame_count;          /**< Method bodies being decoded. */
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
    uint32_t bytes_decoded; /**< AML bytes the walks of every pass decoded, those they stepped over left out (a method
                                 body the declaration pass records, data kept in its table past its head, a construct
                                 left to the interpreter, a duplicate's list, what a fault skips), in a build that
                               counts what runs (§4.1); none for the statements a loop's records are decoded from, so
                               that a run with records and one without do the same work (§8.1). */
    AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t
        predecoding; /**< The records of the loops that loop: last, past what opening the load zeroes. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t;

/**
 * @brief Runs a table's code at load in a frame of its own (§3.6): the statements from @p start to
 *        @p limit, its control flow included, each run of declarations handed to the load as the
 *        interpreter meets it.
 *
 * @details The declaration pass calls it once per table the root table lists; a table Load or LoadTable adds runs
 *          in a frame of the evaluation that runs the operator instead. A statement that fails is counted in
 *          load->evaluation, never as a load error, and the code goes on past it; one whose predicate
 *          fails declares its branches without running their code (§3.6). Its locals last from one
 *          statement to the next, and go with the frame.
 * @param load  The load.
 * @param table The table's index in the load.
 * @param scope The node names are resolved from.
 * @param start Where the code starts.
 * @param limit The first offset it may not reach.
 * @return OK once the code has run, or stopped at a failure it could not go past, counted, every frame it
 *         pushed gone; LIMIT_FRAMES, LIMIT_PENDING_OPERATORS or LIMIT_BLOCKS, counted, when the evaluation has
 *         no room for the table's frame, and nothing ran.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_run_table_term(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table, uint32_t scope, uint32_t start,
    uint32_t limit);

/**
 * @brief Opens, in the running frame of a table's code, the construct the TermList of a Scope, Device, Processor,
 *        PowerResource or ThermalZone the declaration pass enters runs in, as the interpreter opens it for a TermList
 *        handed back to it (§3.6, O21).
 *
 * @param load  The load.
 * @param start Where the statement that declares the scope starts.
 * @param end   Where its TermList ends.
 * @param scope The node its names are resolved from.
 * @return OK, or LIMIT_BLOCKS when the evaluation holds 512 constructs open (§6.1), and nothing opened.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_enter_table_scope(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t start, uint32_t end, uint32_t scope);

/**
 * @brief Closes the construct advanced_configuration_and_power_interface_machine_language_enter_table_scope opened
 *        last, once the declaration pass reaches the end of its TermList, as the interpreter closes it.
 *
 * @param load      The load.
 * @param out_scope Receives the node the construct around it resolves names from.
 * @param out_end   Receives where that construct ends.
 */
extern void advanced_configuration_and_power_interface_machine_language_leave_table_scope(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t *out_scope, uint32_t *out_end);

/**
 * @brief Takes statements of table-level code the interpreter does not run itself, in load->declaration
 *        (§3.6): from @p start, a declaration, data or Noop, a statement whose decoding faults, or one that
 *        failed to run and is stepped over, then every such statement after it up to one the interpreter
 *        runs or the end of the construct; it enters the TermList of each scope it declares, the interpreter's
 *        construct for it opened, and leaves it at its end (O21), where a load whose general_load is set stops
 *        at that TermList instead; or, @p not_run holding, the one If, While or Else at @p start, whose code is
 *        declared without running. A statement waits for a term when load->declaration.asks is not 0 on entry,
 *        which it then goes on from with load->declaration's answers.
 *
 * @param load    The load.
 * @param table   The table's index in the load.
 * @param scope   The node names are resolved from.
 * @param start   Where the statement starts.
 * @param limit   The end of the construct it stands in.
 * @param not_run Whether its code, and that of every TermList inside it, is declared without running.
 */
extern void advanced_configuration_and_power_interface_machine_language_declare_statement(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table, uint32_t scope, uint32_t start,
    uint32_t limit, bool not_run);

/**
 * @brief Whether the interpreter runs a statement of table-level code that starts with @p opcode: one
 *        that is neither a declaration, data, a local, an argument nor Noop.
 *
 * @param opcode A descriptor index.
 * @return true for code.
 */
extern bool advanced_configuration_and_power_interface_machine_language_runs_at_table_level(uint32_t opcode);

/**
 * @brief Whether the needs path executes an opcode where it stands (§4.1): the run list the needs-path closure is
 *        compared with.
 *
 * @param opcode      A descriptor index.
 * @param table_level Whether it stands in table-level code, where the load takes the declarations, rather than in a
 *                    method body.
 * @return true when the interpreter runs it, reads it as a constant, a local, an argument or Debug, or, at table
 *         level, the load declares it; false when it fails with unimplemented_operator there, a data object the
 *         interpreter does not build, such as Revision, at table level too.
 */
extern bool advanced_configuration_and_power_interface_machine_language_executes(uint32_t opcode, bool table_level);

/**
 * @brief Lists the limits of §6.1 a load counted, in the specification's order, each with its hits and its peak: the
 *        record cache's are printed with the records (§3.6), since a load without records holds none. The pending
 *        operators, the blocks and the temporary nodes the decoding pass holds, bounded by the same caps, count in the
 *        hits and the peaks as the evaluations' do.
 *
 * @param load       The load.
 * @param out_limits Receives the limits.
 */
extern void advanced_configuration_and_power_interface_machine_language_limits(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLimit_t
        out_limits[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LIMITS]);

/**
 * @brief Counts a fault of the declaration or the Externals pass, the first one kept.
 *
 * @param load   The load.
 * @param status The fault, not OK.
 */
extern void advanced_configuration_and_power_interface_machine_language_count_load_error(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status);

/**
 * @brief Marks a table whose load met a fault, counted once in table_load_failed when the load declared it from the
 *        directory, as the DSDT and the SSDTs of the root table are (§3.8 step 1).
 *
 * @param load  The load.
 * @param table The table, counted from 1; 0, which names none, is ignored.
 */
extern void advanced_configuration_and_power_interface_machine_language_count_table_failure(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table);

/**
 * @brief Counts a fault in faults, under its status, and logs it as an error, the line §4.5 calls loud: its status,
 *        then the table and the offset of the term that met it and that term's opcode, then the method whose body
 *        holds it, or the scope table-level code runs in, such as
 *        `name_not_found table=SSDT/CpuPm offset=0x0000012F opcode=NameString method=\_PR_.CPU0._PDC`.
 *
 * @details The path is built here alone, from the node: a load that meets no fault builds none.
 * @param load   The load, which counts it, and whose host's log the line goes to.
 * @param status The fault, its counter's name.
 * @param table  The table the term lies in, counted from 1, or 0 for none.
 * @param offset Where the term starts in that table.
 * @param end    The end of the construct the term stands in: a term at or past it is named `End`.
 * @param method The node of the method whose body holds the term, or 0 for table-level code.
 * @param scope  The node the code runs in, named when @p method is 0.
 */
extern void advanced_configuration_and_power_interface_machine_language_report_fault(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status, uint32_t table, uint32_t offset, uint32_t end,
    uint32_t method, uint32_t scope);

/**
 * @brief Counts in faults, under its status, a fault a host's evaluation meets outside any term, such as the read of a
 *        field in a space without a handler, or a call of a method without a body, and logs it as an error: its
 *        status, then the object the host evaluated, such as `space_unsupported object=\_SB_.PCI0.EC0_.FLD0`.
 *
 * @param load   The load, which counts it, and whose host's log the line goes to.
 * @param status The fault, its counter's name.
 * @param node   The object the host evaluated.
 */
extern void advanced_configuration_and_power_interface_machine_language_report_object_fault(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status, uint32_t node);

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
 * @brief Whether reading a field unit gives an Integer (§5.3) and writes nothing: it holds a unit of a Field, never
 *        of an IndexField or a BankField, whose read writes its index or its bank, of a valid AccessType, no wider than
 *        the load's integers.
 *
 * @param load The load.
 * @param node The node.
 * @return Whether the node's read is an Integer.
 */
extern bool advanced_configuration_and_power_interface_machine_language_field_reads_integer(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node);

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
 * @return OK; TYPE_MISMATCH when @p region holds no PCI_Config region; OUT_OF_MEMORY or LIMIT_OBJECT_RUNS when the
 *         heap cannot hold an object it reads, its data kept in its table (O20); kept data whose bytes build no object
 *         is left Uninitialized, and read as such.
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
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint64_t firmware_control,
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
 *        255 µs the host busy-waits them, beyond it sleeps them; both are counted beyond 100 µs.
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
 * @brief Makes the node a Mutex in a method declared, a temporary (§5.1), a mutex of the SyncLevel @p sync_flags give.
 *
 * @param load       The load.
 * @param node       The temporary node.
 * @param sync_flags The Mutex's SyncFlags: its SyncLevel in bits 0 to 3.
 */
extern void advanced_configuration_and_power_interface_machine_language_declare_mutex(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node, uint8_t sync_flags);

/**
 * @brief Gives back every acquisition the evaluation still holds of @p node, a temporary whose frame exits, before the
 *        node goes (§5.1): counted with the mutexes the end of an evaluation releases (§5.2); nothing when the
 *        evaluation holds none of it.
 *
 * @param load The load.
 * @param node The temporary node.
 */
extern void advanced_configuration_and_power_interface_machine_language_release_temporary_mutex(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node);

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
 * @brief Runs Reset (§5.2): the Event's pending signals are cleared.
 *
 * @param load The load.
 * @param node The object named.
 * @return OK; TYPE_MISMATCH when the object is no Event.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_reset(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node);

/**
 * @brief Runs Wait (§5.2): takes one of the Event's pending signals, or, with none, reports a timeout at once,
 *        whatever the timeout, since nothing else can signal while the only AML owner waits.
 *
 * @param load          The load.
 * @param node          The object named.
 * @param out_timed_out Receives whether no signal was pending; untouched on a refusal.
 * @return OK; TYPE_MISMATCH when the object is no Event.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_wait(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t node, bool *out_timed_out);

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
 *         data; METHOD_WITHOUT_BODY for a method whose declaration recorded no body, whatever the arguments;
 *         why the method failed otherwise, every frame it pushed gone by then; OK on success.
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
 * @details A failure is logged once (§4.5): at the term that met it, or, when no term did, with the object
 *          (advanced_configuration_and_power_interface_machine_language_report_object_fault).
 * @param load           A load whose tables are decoded, its stacks free.
 * @param node           The node, which holds the object.
 * @param arguments      The method's arguments, or NULL when it takes none.
 * @param argument_count How many: exactly as many as the method takes (§5.2).
 * @param out_result     Receives the result, which the caller gives back with
 *                       advanced_configuration_and_power_interface_machine_language_value_release.
 * @return TYPE_MISMATCH for the wrong number of arguments or an object that is no data; METHOD_WITHOUT_BODY
 *         for a method whose declaration recorded no body, whatever the arguments; why the method failed
 *         otherwise, every frame it pushed gone by then; OK on success.
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
 *        there is one, then runs the decoding pass, unless the load skips bodies: every term is decoded and
 *        counted, method bodies included, and each fault is recorded in its table and skipped as the
 *        declaration pass skips it.
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
 * @brief Gives the load a directory, which a memory write is checked against from then on (§5.3) and
 *        LoadTable searches once no table is declared, and the global lock of its FACS, then declares its
 *        DSDT and every SSDT: the code of the first table already runs with the directory.
 *
 * @details Each table's checksum is the one the directory computed when it recorded the table: the
 *          load does not sum its bytes again.
 *
 * @param load      An open load, with no table yet.
 * @param directory The directory, whose tables stay mapped until the load is closed.
 */
extern void advanced_configuration_and_power_interface_machine_language_declare_directory(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory);

/**
 * @brief Loads the DSDT, then every SSDT, of a directory: every pass over every table, the decoding pass
 *        excepted when the load skips bodies.
 *
 * @param load      An open load, with no table yet.
 * @param directory The directory, whose tables stay mapped until the load is closed.
 */
extern void advanced_configuration_and_power_interface_machine_language_load_directory(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory);

/**
 * @brief Runs Load on the object a node holds (§5.1): the table a SystemMemory region or a field unit
 *        holds is copied into the namespace arena, its checksum computed on the copy, and added last to the
 *        registry, for the evaluation that runs the Load to run its code; a table the load holds already is
 *        refused.
 *
 * @param load       The load, in an evaluation.
 * @param source     The node of the region or of the field unit.
 * @param out_loaded Receives whether the table was added: false for a table the load holds already.
 * @return OK; LOAD_FORM_UNSUPPORTED for a node that holds a Buffer or a buffer field, or while a table is
 *         declared;
 *         TYPE_MISMATCH for any other object or a region of another space; REGION_UNEVALUATED for a
 *         region whose address was never evaluated; REGION_BOUNDS for a table shorter than its header
 *         or longer than what holds it; what reading it failed with; LIMIT_TABLES, LIMIT_TABLE_BYTES or
 *         OUT_OF_MEMORY when it cannot be kept.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_load_from(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t source, bool *out_loaded);

/**
 * @brief Runs LoadTable with an empty RootPath, ParameterPath and ParameterData (§5.1): the first table of
 *        the directory with that signature, and that OEM ID and OEM table ID unless they are empty, each
 *        compared with its field padded with zeros, is added last to the registry as Load adds a table it
 *        copied, and runs in place.
 *
 * @param load       The load, in an evaluation.
 * @param signature  The SignatureString's bytes.
 * @param oem        The OEMIDString's bytes.
 * @param oem_table  The OEMTableIDString's bytes.
 * @param lengths    The lengths of the signature, the OEM ID and the OEM table ID.
 * @param out_loaded Receives whether a table was added: false when none matches or the load holds it already.
 * @return OK; LOAD_FORM_UNSUPPORTED while a table is declared; TYPE_MISMATCH for a signature of another
 *         length than 4, an OEM ID longer than 6 or an OEM table ID longer than 8; LIMIT_TABLES.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_load_from_directory(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, const uint8_t *signature, const uint8_t *oem,
    const uint8_t *oem_table, const uint32_t lengths[3], bool *out_loaded);

/**
 * @brief Declares the code of a table Load or LoadTable added without running it (§3.6): every branch as one whose
 *        predicate failed when the evaluation has no room for the table's frame, every branch where the load
 *        declares every branch.
 *
 * @param load  The load, in an evaluation.
 * @param table The table's index in the load.
 */
extern void advanced_configuration_and_power_interface_machine_language_declare_loaded_table(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table);

/**
 * @brief Ends the load of a table Load or LoadTable added once its code is declared: places the Externals no table
 *        could place yet, then decodes its method bodies unless the load skips bodies (§3.6).
 *
 * @param load  The load.
 * @param table The table's index in the load.
 */
extern void advanced_configuration_and_power_interface_machine_language_finish_loaded_table(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table);

/**
 * @brief Reads memory through the mapping cache, as regions read it: a 4-byte access where the address
 *        and what is left allow it, a byte otherwise, so that no access is unaligned.
 *
 * @param load             The load.
 * @param physical_address The first byte.
 * @param length           Bytes to read.
 * @param destination      Receives them.
 * @return OK; ADDRESS_UNREACHABLE for a byte past what the host's target reaches, 4 GiB on i686, or a page the host
 *         does not map.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_memory_read(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint64_t physical_address, uint32_t length,
    uint8_t *destination);

/**
 * @brief Builds the data object a Name kept in its table (O20), once a name resolution reaches its node: the
 *        Package, VarPackage or Buffer the load would have built, from the same bytes, its names read in the
 *        node's parent and its integers as wide as when its table was declared, by a builder of its own (C4); by
 *        the general data decoder when the load's general_load is set, or when the bytes do not read as kept data,
 *        which the load did not check or AML has rewritten since. Bytes that build no object, for a fault or a term
 *        that needs the interpreter, leave the node Uninitialized, as a load that built the object as it declared it
 *        would have left it, counted in data_faults_read or values_deferred. Any other node is left as it is.
 *
 * @param load  The load.
 * @param index The node a name resolved to.
 * @return OUT_OF_MEMORY or LIMIT_OBJECT_RUNS when the heap cannot hold the object, the node keeping its data; OK
 *         otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_materialize(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t index);

/**
 * @brief Reads what the head of a data object kept in its table (O20) declares, without building it: a Package's or a
 *        VarPackage's element count, or a Buffer's length and the initialiser its bytes start with, zeros after it;
 *        an object whose elements build nothing reads as Uninitialized once built.
 *
 * @param load            The load.
 * @param value           A node's value.
 * @param out_length      Receives the elements of a package, the bytes of a buffer.
 * @param out_initialiser Receives a buffer's initialiser, NULL for a package.
 * @param out_initialised Receives the bytes of that initialiser, 0 for a package.
 * @return false when @p value holds no kept data.
 */
/**
 * @brief Adds node @p index to @p signature, a sum over a set of nodes that does not depend on their order: the FNV-1a
 *        fold of the index's four bytes, so that two sets of nodes differ in their sums but by chance.
 *
 * @param signature The sum so far, 0 for an empty set.
 * @param index     A node index.
 * @return The sum with the node added.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_kept_data_signature(uint32_t signature,
                                                                                                uint32_t index);

extern bool advanced_configuration_and_power_interface_machine_language_describe_kept_data(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value, uint32_t *out_length,
    const uint8_t **out_initialiser, uint32_t *out_initialised);

/**
 * @brief Folds a load's namespace as the parity record of the specification (§8.7) does: FNV-1a,
 *        32 bits, over a depth-first walk in declaration order, External nodes apart. Per node: its
 *        depth, its name and its type, then an Integer; a String's or a Buffer's length and bytes; a
 *        Package's count and each element the same way, its kind first (an uninitialised one 0), a name element as its
 *        path bytes; a method's flags and body length; a region's space, offset and length; a field unit's kind,
 *        bit offset, bit length, access and update rule; a Processor's ID, block address and block
 *        length. A data object kept in its table folds as the object it builds into. Nothing that holds an
 *        address or an index is folded.
 *
 * @param load A load whose tables are decoded.
 * @return The fold.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_namespace_fold(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Folds the evaluations record of the specification (§8.7): FNV-1a, 32 bits, over each path of @p paths in
 *        order, evaluated with no argument: its status, two bytes, then, once it succeeded, its result as the
 *        namespace fold folds a node's type and object: the type, one byte, then an Integer's eight bytes; a String's
 *        or a Buffer's length and bytes; a Package's count and each element the same way, its kind first, a name
 *        element as its path bytes. Nothing that holds an address or an index is folded.
 *
 * @details Each result is given back once folded. The evaluations run as any other, so they change the namespace and
 *          the counters as the methods they call do.
 * @param load          A load whose tables are decoded, its stacks free.
 * @param paths         Absolute paths, such as `\_SB_.PCI0._HID`.
 * @param count         How many.
 * @param out_succeeded Receives how many of them evaluated with OK.
 * @return The fold.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_evaluations_fold(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, const char *const *paths, uint32_t count,
    uint32_t *out_succeeded);

/**
 * @brief Folds the work record of the specification (§8.7): FNV-1a, 32 bits, over the operations executed per
 *        opcode, four little-endian bytes each in descriptor order, then four bytes each of the lookups, the probes,
 *        the objects allocated, the bytes decoded and the maps its cache asked the host for.
 *
 * @details The operations executed and the bytes decoded are counted only in a build that counts what runs (§4.1):
 *          elsewhere they fold as zeros.
 * @param load A load.
 * @return The fold.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_work_fold(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Logs a Fatal that code ran (§5.2) as an error under its counter, fatals, with the fields §4.5 gives a fault:
 *        its table, its offset, its opcode, and its method or, in table-level code, its scope.
 *
 * @param load   The load, whose host's log the line goes to.
 * @param table  The table the Fatal lies in, counted from 1.
 * @param offset Where its opcode lies in that table.
 * @param method The method that ran it, or 0 in table-level code.
 * @param scope  The scope table-level code runs it in, named when @p method is 0.
 */
extern void advanced_configuration_and_power_interface_machine_language_log_fatal(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table, uint32_t offset,
    uint32_t method, uint32_t scope);

/**
 * @brief Gives a load's pages back to its host.
 *
 * @param load The load; its nodes must not be used afterwards.
 */
extern void advanced_configuration_and_power_interface_machine_language_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_H */
