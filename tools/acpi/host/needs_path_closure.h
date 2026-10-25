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
 * @file needs_path_closure.h
 * @brief The static closure of the needs path: the opcodes the interpreter executes.
 *
 * The needs path (specification §1.3) starts from the table-level code of every definition block
 * and from the methods the kernel evaluates: `_INI`, `_REG`, `_STA` and `_CST` anywhere, `_OSC` of
 * `\_SB_` or of a processor, `_PDC` of a processor. The closure follows every call through every
 * branch, as the decode pass of a load sees them, and gathers the opcodes the bodies it reaches
 * hold. Any other opcode fails with `unimplemented_operator` (§4.1). It is a host tool: the
 * interpreter is written to the closure of the corpus, never computed at boot.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-25
 **************************************************************************/

#ifndef TOOLS_ACPI_HOST_NEEDS_PATH_CLOSURE_H
#define TOOLS_ACPI_HOST_NEEDS_PATH_CLOSURE_H

#include <kernel/acpi/machine_language.h>

#include <stdbool.h>
#include <stdint.h>

#define NEEDS_PATH_CLOSURE_OPCODE_WORDS                                                                                \
    ((ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT + 31u) / 32u) /**< Words of one opcode  \
                                                                                                set. */

/**
 * @brief What a load's decode pass told the closure, and what the last walk found.
 *
 * @details Methods are keyed by node index. A method a method body declares is a temporary node,
 *          whose index a later node may take again: its opcodes and calls then count for that node.
 */
typedef struct NeedsPathClosure {
    uint32_t (*opcodes)[NEEDS_PATH_CLOSURE_OPCODE_WORDS];      /**< Per node, 0 for table-level code: the opcodes its
                                                                    body holds, as bits. */
    bool *reached;                                             /**< Per node: whether the walk reached that method. */
    uint32_t node_capacity;                                    /**< Nodes the two arrays cover. */
    uint32_t *calls;                                           /**< Caller, then callee, for each call. */
    uint32_t call_count;                                       /**< Calls recorded. */
    uint32_t call_capacity;                                    /**< Calls @p calls holds. */
    uint32_t dropped;                                          /**< Opcodes and calls that could not be kept. */
    uint32_t entries;                                          /**< Entries the last walk started from. */
    uint32_t methods;                                          /**< Methods it reached, table-level code apart. */
    uint32_t reached_opcodes[NEEDS_PATH_CLOSURE_OPCODE_WORDS]; /**< The opcodes it reached, as bits. */
} NeedsPathClosure_t;

/**
 * @brief Makes the closure the observer of a load that is open and has decoded nothing yet.
 *
 * @param closure Filled; released with needs_path_closure_release().
 * @param load    The load whose decode pass the closure records.
 * @return false when the closure's arrays could not be allocated; the load is then left unobserved.
 */
extern bool needs_path_closure_attach(NeedsPathClosure_t *closure,
                                      AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Walks the needs path of a load that has decoded its tables: marks its entries and the table-level
 *        code, follows every recorded call until nothing new is reached, and gathers their opcodes.
 *
 * @param closure Attached to @p load.
 * @param load    The load, its tables decoded.
 */
extern void needs_path_closure_walk(NeedsPathClosure_t *closure,
                                    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load);

/**
 * @brief Whether the last walk reached an opcode.
 *
 * @param closure Walked.
 * @param opcode  A descriptor.
 * @return true when a body the walk reached, or table-level code, holds @p opcode.
 */
extern bool needs_path_closure_reaches_opcode(const NeedsPathClosure_t *closure, uint32_t opcode);

/**
 * @brief Frees what needs_path_closure_attach() allocated and empties the closure.
 *
 * @param closure Attached, or zeroed.
 */
extern void needs_path_closure_release(NeedsPathClosure_t *closure);

#endif /* TOOLS_ACPI_HOST_NEEDS_PATH_CLOSURE_H */
