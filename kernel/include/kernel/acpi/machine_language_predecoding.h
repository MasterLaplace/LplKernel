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
 * @file machine_language_predecoding.h
 * @brief The records of pre-decoded loops: what a statement a While runs is, decoded once.
 *
 * Part of layer L3 of the specification (docs/acpi/specification.md, §3.6 and §6.1). A record is a fact
 * about the bytes of a table: the statement that starts at an offset, under the end of the construct
 * around it, has a shape the interpreter can run without decoding it again. Records are built the first
 * time a loop that loops reaches each statement, kept in a cache of the load, and found by their table
 * and offset; the interpreter runs them, and hands a statement back to its own loop whenever a record
 * cannot run it as that loop would.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-03
 **************************************************************************/

#ifndef KERNEL_ACPI_MACHINE_LANGUAGE_PREDECODING_H
#define KERNEL_ACPI_MACHINE_LANGUAGE_PREDECODING_H

#include <kernel/acpi/machine_language_decoder.h>
#include <kernel/acpi/machine_language_namespace.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORDS                                        \
    128u /**< Records a load keeps (§6.1); a full cache is emptied whole. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_NODES                                   \
    256u /**< Operators the records of a load hold, over every record. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_CONSTANTS                               \
    256u /**< Constants the records of a load hold, over every record. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_SLOTS                                       \
    256u /**< Slots of the hash that finds a record by its table and offset: twice the records. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECENT_RECORDS                                     \
    64u /**< Recent records a cache keeps, one per offset modulo this count: the last found or built at such an        \
           offset. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NODES_PER_RECORD                               \
    8u /**< Operators one record's expression holds at most; a longer one is run by the interpreter's loop. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_NAMES                                   \
    64u /**< Names the records of a load read, over every record. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NAMES_PER_RECORD                               \
    4u /**< Names one record reads at most; a statement that reads more is run by the interpreter's loop. */

/**
 * @brief What a record runs: the shapes from STORE on write a local or an argument.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordShape {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_GENERAL = 0,   /**< Nothing: the interpreter's
                                                                                         loop runs the statement. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_WHILE = 1,     /**< A While and its predicate. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_IF = 2,        /**< An If and its predicate. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_BREAK = 3,     /**< A Break. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_STORE = 4,     /**< A Store of its source, or an
                                                                                         operator on two integers, to
                                                                                         its target. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_INCREMENT = 5, /**< An Increment of its
                                                                                         target. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_DECREMENT = 6, /**< A Decrement of its
                                                                                         target. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordShape_t;

/**
 * @brief Where an operand of a record comes from.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperandKind {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_NONE = 0,     /**< No operand. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_CONSTANT = 1, /**< A constant: index is
                                                                                                its place among the
                                                                                                cache's constants. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_LOCAL = 2, /**< A LocalX: slot is X. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_ARGUMENT = 3, /**< An ArgX: slot is X. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_RESULT = 4,   /**< What an earlier
                                                                                                operator of the same
                                                                                                record gives: slot is
                                                                                                its place in the
                                                                                                record. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_NAME = 5, /**< A name read as a value:
                                                                                            slot is its place among
                                                                                            the record's names, index
                                                                                            its place among the
                                                                                            cache's. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperandKind_t;

/**
 * @brief Why the interpreter ran a statement a record was found for itself, counted per reason.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordDecline {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_SHAPE = 0,     /**< No record runs it. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_LIMIT = 1,     /**< The record was built
                                                                                           under another end of the
                                                                                           construct around it. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_OPERAND = 2,   /**< An operand held no
                                                                                           Integer. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_ROOM = 3,      /**< A stack of the evaluation
                                                                                           would overflow. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_OPERATION = 4, /**< An operator would fail:
                                                                                           a modulo by zero. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_FULL = 5,      /**< The cache was full, or
                                                                                           frozen for this loop after
                                                                                           it was emptied. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_TARGET = 6,    /**< The target held more than
                                                                                           an Integer or nothing, an
                                                                                           object to give back or a
                                                                                           reference to go through,
                                                                                           or no Integer to step. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_NAME = 7,      /**< A name it reads named no
                                                                                           object, a method, an
                                                                                           object whose read gives no
                                                                                           Integer, or a field unit
                                                                                           whose PCI_Config region
                                                                                           waits for its attach. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINES = 8,           /**< Reasons counted. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordDecline_t;

/**
 * @brief An operand of a record.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperand {
    uint8_t kind;   /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_* kind. */
    uint8_t slot;   /**< A local's or an argument's number, or an operator's place in its record. */
    uint16_t index; /**< A constant's place among the cache's constants. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperand_t;

/**
 * @brief A name a record reads, and what the record knows of it: the node it named when it was last
 *        checked, which holds while the namespace's generation and the scope do not change.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordName {
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t name; /**< Its NameString, in its table. */
    uint32_t after;           /**< Where its NameString ends: the pc the loop leaves when reading it fails. */
    uint32_t node;            /**< The node it named when last checked, or 0. */
    uint32_t generation;      /**< The namespace's generation when it was checked. */
    uint32_t scope;           /**< The scope it was resolved from. */
    uint8_t values_before;    /**< Values the loop held for its statement at most before reading it. */
    uint8_t nodes_before;     /**< In a build that counts what the loop runs, the operators of its record the loop runs
                                   before reading it: the first ones, in the record's order. */
    uint8_t operators_before; /**< Operators the loop held pending for its statement at most before reading it. */
    uint8_t reserved;         /**< Unused, 0. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordName_t;

/**
 * @brief An operator of a record's expression, in the order the interpreter's loop runs them.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode {
    uint16_t opcode;  /**< Its descriptor: a comparison, LNot, or an operator on two integers. */
    uint8_t negated;  /**< For a comparison an LNot takes at once, 1: the two run as one (§7.2, O7). */
    uint8_t reserved; /**< Unused, 0. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperand_t operands[2]; /**< Its operands. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t;

/**
 * @brief The statement at an offset of a table, under the end of the construct around it, decoded
 *        (§3.4, §3.6).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageRecord {
    uint32_t pc;         /**< Where the statement starts in its table. */
    uint32_t after;      /**< Where the next statement starts: for an If or a While, its body. */
    uint32_t end;        /**< For an If or a While, its PkgLength end. */
    uint32_t limit;      /**< The end of the construct around the statement when it was decoded. */
    uint16_t table;      /**< Its table, counted from 1. */
    uint16_t next;       /**< The record at after, plus 1, once found; 0 when not found yet. */
    uint16_t first_node; /**< Its first operator among the cache's. */
    uint8_t node_count;  /**< Its operators. */
    uint8_t shape;       /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_* shape. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperand_t source; /**< The predicate of an If or a
                                                                                    While, the value a Store or an
                                                                                    operator stores. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperand_t target; /**< The local or argument a Store,
                                                                                    an operator, an Increment or a
                                                                                    Decrement writes, or none. */
    uint8_t values_peak;    /**< Values the interpreter's loop holds on its stack for it at most. */
    uint8_t operators_peak; /**< Operators it holds pending for it at most. */
    uint8_t tight;          /**< For a While, 1 once linked to the record of a body that makes it a tight loop: its
                                 predicate compares two leaves, and its body is one statement that writes. */
    uint8_t goes_on;        /**< For a record that runs nothing, 1 when its statement always goes on to the one after
                                 it: a call, a name, or an operator that opens no construct and is no Break, Continue
                                 or Return. */
    uint16_t first_name;    /**< Its first name among the cache's. */
    uint8_t name_count;     /**< The names it reads, in the order the loop reads them. */
    uint8_t statement;      /**< In a build that counts what the loop runs, the opcode of its statement when no
                                 operator of the record is one: While, If, Break, Store, Increment or Decrement; 0 for
                                 an operator that stands as a statement. A record that runs nothing counts nothing. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRecord_t;

/**
 * @brief The records of a load, and what running them counted (§3.5, §6.1). It lies past the part of
 *        the load that opening it zeroes: opening it empties the hash and the counters alone, and a
 *        record is one only below the count and under its own key.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding {
    uint32_t record_count;   /**< Records built since the cache was last emptied. */
    uint32_t node_count;     /**< Operators those records hold. */
    uint32_t constant_count; /**< Constants those records hold. */
    uint32_t name_count;     /**< Names those records read. */
    uint8_t integer_bits;    /**< The integer width the records were built under. */
    uint32_t frozen_serial;  /**< The frame whose loop builds no record since the cache was emptied under it. */
    uint32_t frozen_start;   /**< Where that loop starts. */
    uint32_t watched_serial; /**< The frame whose loop handed every statement back at once since its last back-edge,
                                  each going on to the next; 0 when none. */
    uint32_t watched_start;  /**< Where that loop starts. */
    uint32_t records_run;    /**< Statements and predicates run from records. */
    uint32_t declined[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINES]; /**< Statements handed back
                                                                                                to the interpreter's
                                                                                                loop, per reason. */
    uint32_t built;                                                                          /**< Records built. */
    uint32_t flushes;           /**< Times the cache was emptied whole. */
    uint32_t records_peak;      /**< Most records held at once. */
    uint32_t nodes_peak;        /**< Most operators those records held at once. */
    uint32_t constants_peak;    /**< Most constants they held at once. */
    uint32_t names_peak;        /**< Most names they read at once. */
    uint32_t fused_comparisons; /**< LNot and a comparison decoded as one operator (§7.2, O7). */
    uint32_t tight_turns;       /**< Turns of a While run as a tight loop (§3.6). */
    uint32_t searches;          /**< Searches of the hash for a statement that neither a link nor its recent slot gave,
                                     found or not. */
    uint32_t handed_back_loops; /**< Times a frame's loop was handed back whole, its turns then asking no record. */
    uint32_t stale_names;       /**< Names whose counted resolution found another node than the one their record
                                     checked: 0 unless a change of the namespace escaped its generation. */
    union {
        uint16_t slots[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_SLOTS]; /**< A record, plus 1,
                                                                                                     per hash slot. */
        uint32_t slot_words[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_SLOTS /
                            2u]; /**< The same slots, two to a word, as emptying writes them. */
    };
    uint16_t
        recent[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECENT_RECORDS]; /**< The record, plus 1,
                                                                                              last found or built
                                                                                              at an offset of each
                                                                                              slot; one only below
                                                                                              the count and under
                                                                                              the offset's key, so
                                                                                              never emptied. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageRecord_t
        records[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORDS]; /**< The records. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t
        nodes[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_NODES]; /**< Their operators. */
    uint64_t constants
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_CONSTANTS]; /**< Their constants, masked
                                                                                               to the width. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordName_t
        names[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_NAMES]; /**< The names they read.
                                                                                              */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t
        name_values[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NAMES_PER_RECORD]; /**< What the
                                                                                                        record running
                                                                                                        read by name,
                                                                                                        in its order. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t;

/**
 * @brief Empties the records and zeroes what they counted, as opening a load does; nothing else of
 *        the cache is read before it is written.
 *
 * @param cache The cache.
 */
extern void advanced_configuration_and_power_interface_machine_language_predecoding_open(
    AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t *cache);

/**
 * @brief Empties the records whole, counted when it held one, and keeps what they counted; the cache
 *        then holds the records of @p integer_bits.
 *
 * @param cache        The cache.
 * @param integer_bits The load's integer width.
 */
extern void advanced_configuration_and_power_interface_machine_language_predecoding_flush(
    AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t *cache, uint8_t integer_bits);

/**
 * @brief Whether a record writes a local, an argument or NullName: a Store, an operator with a target, an
 *        Increment or a Decrement.
 *
 * @param shape An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_* shape.
 * @return Whether it is one of those three.
 */
static inline bool advanced_configuration_and_power_interface_machine_language_record_writes(uint8_t shape);

/**
 * @brief Links record @p from to record @p to, the record of the statement where @p from leaves the frame's pc,
 *        and decides once whether @p from, a While, runs as a tight loop with @p to as its body (§3.6): its
 *        predicate compares two leaves, and its body is one statement that writes and ends where the While does.
 *
 * @param cache The cache.
 * @param from  The record linked, plus 1.
 * @param to    The record it leads to, plus 1.
 */
extern void advanced_configuration_and_power_interface_machine_language_record_link(
    AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t *cache, uint16_t from, uint16_t to);

/**
 * @brief The record of the statement at @p pc of table @p table when it is the recent record of its offset's slot,
 *        without a search.
 *
 * @param cache The cache.
 * @param table The table, counted from 1.
 * @param pc    The statement's offset.
 * @return The record, plus 1; 0 when the slot holds another, or none.
 */
static inline uint16_t advanced_configuration_and_power_interface_machine_language_record_recent(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t *cache, uint16_t table, uint32_t pc);

/**
 * @brief The record of the statement at @p pc of table @p table, searched in the hash, which becomes the recent
 *        record of its offset's slot.
 *
 * @param cache The cache.
 * @param table The table, counted from 1.
 * @param pc    The statement's offset.
 * @return The record, plus 1; 0 when the cache holds none.
 */
extern uint16_t advanced_configuration_and_power_interface_machine_language_record_find(
    AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t *cache, uint16_t table, uint32_t pc);

/**
 * @brief Decodes the statement at @p pc of a table under @p limit, the end of the construct around it,
 *        into a record: a While, an If or a Break whose predicate is an expression over locals,
 *        arguments and constants, its operators the comparisons, LNot and the operators on two
 *        integers, with no target but NullName; a Store of such an expression to a local or an
 *        argument; an operator on two integers whose target is a local, an argument or NullName; an
 *        Increment or a Decrement of a local or an argument. Any other statement, or one whose decoding
 *        faults, becomes a record the interpreter's loop runs. The record becomes the recent record of its
 *        offset's slot.
 *
 * @param cache        The cache, whose records are of @p integer_bits.
 * @param code         The table's bytes, header included.
 * @param table        The table, counted from 1.
 * @param pc           Where the statement starts; no record of the cache is at it.
 * @param limit        The end of the construct around it.
 * @param integer_bits The load's integer width, 32 or 64.
 * @return The new record, plus 1; 0 when the cache has no room for it, its records, its operators or its
 *         constants full, nothing built.
 */
extern uint16_t advanced_configuration_and_power_interface_machine_language_record_build(
    AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t *cache, const uint8_t *code, uint16_t table,
    uint32_t pc, uint32_t limit, uint8_t integer_bits);

#include <kernel/acpi/machine_language_predecoding.inl>

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_PREDECODING_H */
