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
 * @file shortcuts.h
 * @brief One-byte changes of a table, each loaded with the declaration pass's shortcuts and without.
 *
 * The host changes every byte of a table to each of three values, ring 0 a sample of them
 * (docs/acpi/specification.md, §3.6 and §8.1).
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-03
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_SHORTCUTS_H
#define KERNEL_TESTS_ACPI_SHORTCUTS_H

#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief The tables whose bytes the changes reach.
 */
typedef enum ShortcutsTable {
    SHORTCUTS_TABLE_DATA,       /**< Names of every kind of constant data. */
    SHORTCUTS_TABLE_STATEMENTS, /**< Statements of every shape the declaration pass's decoder takes (O19). */
    SHORTCUTS_TABLE_REGIONS,    /**< OperationRegions the decoder takes, hands back, or leaves to the walk (O19). */
    SHORTCUTS_TABLE_SCOPES, /**< Table-level code inside the nested TermLists of the scopes a table declares (O21). */
} ShortcutsTable_t;

/**
 * @brief What only the load's shortcuts change: what each of them took.
 */
typedef struct ShortcutsTaken {
    uint32_t data_stepped_over;             /**< Data objects of a Name stepped over (O18). */
    uint32_t declarations_decoded_directly; /**< Statements the declaration pass's decoder took (O19). */
    uint32_t integers_built_directly;       /**< Integer constants of a Name the decoder built itself (O19). */
    uint32_t scopes_entered;                /**< TermLists of table-level code the declaration pass entered (O21). */
} ShortcutsTaken_t;

/**
 * @brief What the loads of a table's changes ended with.
 */
typedef struct ShortcutsMutations {
    uint32_t bytes;         /**< Bytes of the table. */
    uint32_t loads;         /**< Changes loaded both ways. */
    uint32_t disagreements; /**< Of those, the ones whose two loads differ. */
    uint32_t data_kept;     /**< Data objects of a Name kept in their table (O20) over all of them, by the loads with
                                 the shortcuts, as many as those without. */
    ShortcutsTaken_t taken; /**< What the shortcuts took over all of them. */
} ShortcutsMutations_t;

/**
 * @brief Loads @p table both ways once for each change of one of its bytes, every @p stride bytes from the
 *        first, and compares the namespace, its fold, the faults and the counts of what the two loads declared, not
 *        their work (lookups, probes, the stacks' peaks), which the shortcuts lower.
 *
 * @details A byte is changed to its value with one bit flipped, to 0x00 and to 0xFF when @p every_value
 *          holds, else to one of the three in turn from one changed byte to the next; to the flipped value
 *          where the value is the byte's own.
 * @param test          The test the loads report to.
 * @param table         The table whose bytes change.
 * @param stride        Bytes from one changed byte to the next, 1 for every byte.
 * @param every_value   Whether each byte takes the three values, or one.
 * @param out_mutations Receives the table's length, the loads, the disagreements and what the shortcuts took.
 */
extern void shortcuts_change_bytes(KernelTest_t *test, ShortcutsTable_t table, uint32_t stride, bool every_value,
                                   ShortcutsMutations_t *out_mutations);

#endif /* KERNEL_TESTS_ACPI_SHORTCUTS_H */
