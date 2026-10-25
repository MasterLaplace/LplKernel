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
 * @file machine_language_interpreter.h
 * @brief What running AML holds: frames, the value stack, and what an evaluation counted.
 *
 * Part of layer L3 of the specification (docs/acpi/specification.md, §3.4 and §3.6). One loop runs
 * AML straight from its table, with explicit stacks and no C recursion: every stack is a fixed
 * array sized by the limits of §6.1, and a limit reached fails the evaluation, never the kernel.
 * Integers are values, so an evaluation that only computes integers allocates nothing.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-25
 **************************************************************************/

#ifndef KERNEL_ACPI_MACHINE_LANGUAGE_INTERPRETER_H
#define KERNEL_ACPI_MACHINE_LANGUAGE_INTERPRETER_H

#include <kernel/acpi/machine_language_decoder.h>
#include <kernel/acpi/machine_language_objects.h>

#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES                              \
    64u /**< Frames one evaluation holds, a table's own included (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES                                         \
    1024u /**< Values on the value stack (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS                           \
    256u /**< Operators an evaluation holds pending (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_BLOCKS                              \
    512u /**< Constructs an evaluation holds open (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_LOOP_ITERATIONS                                \
    (1u << 20u) /**< While back-edges one evaluation takes (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FIELD_ACCESSES                                 \
    (1u << 20u) /**< Field accesses one evaluation makes (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ARGUMENTS 7u /**< Arg0 to Arg6. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LOCALS    8u /**< Local0 to Local7. */

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
 * @brief A running method body, or the code of a table at load (§3.4).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageFrame {
    const uint8_t *code; /**< The table it runs in, header included: the only pointer, never folded. */
    uint32_t pc;         /**< The next byte to run, saved while a callee runs. */
    uint32_t end;        /**< The end of its body. */
    uint32_t method;     /**< The method's node, 0 for a table's code. */
    uint32_t scope;      /**< Where its names are resolved from. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t
        arguments[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ARGUMENTS]; /**< Arg0 to Arg6. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t
        locals[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LOCALS]; /**< Local0 to Local7. */
    uint16_t value_base;                                                            /**< Values its callers hold. */
    uint16_t operator_base;                                                         /**< Operators its callers hold. */
    uint16_t block_base;                                                            /**< Blocks its callers hold. */
    uint16_t temporary_base; /**< Temporary nodes alive when it began, removed down to this when it ends. */
    uint16_t table;          /**< Its table, counted from 1. */
    uint8_t argument_count;  /**< Arguments it was called with. */
    uint8_t reserved;        /**< 0. */
    uint32_t serial; /**< Which frame this is, among all an evaluation context pushed: a reference to one of its locals
                        or arguments names it, so that one that outlives it refers to nothing. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageFrame_t;

/**
 * @brief The stacks every evaluation shares, allocated once with the load and never zeroed between
 *        evaluations, and what the evaluations counted (§3.4, §6.1).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation {
    AdvancedConfigurationAndPowerInterfaceMachineLanguageFrame_t
        frames[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES]; /**< The frames. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t
        values[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES]; /**< Operands not consumed
                                                                                            yet. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator_t
        operators[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS]; /**< Operators
                                                                                                            waiting for
                                                                                                            operands. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageBlock_t
        blocks[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_BLOCKS]; /**< Constructs
                                                                                                      open. */
    uint32_t frame_count;            /**< Frames in use. */
    uint32_t value_count;            /**< Values in use. */
    uint32_t operator_count;         /**< Operators in use. */
    uint32_t block_count;            /**< Blocks in use. */
    uint32_t iterations;             /**< Back-edges the current evaluation took: a table's code and the methods it
                                          calls count as one. */
    uint32_t field_accesses;         /**< Field accesses the current evaluation made (§6.1): each access to a
                                          region, and each read of the global lock word while taking it. */
    uint32_t runs;                   /**< Statements and predicates of table-level code run. */
    uint32_t failures;               /**< Of those, the ones that failed. */
    uint8_t first_failure;           /**< The status of the first failure. */
    uint32_t implicit_returns;       /**< Methods that ended without Return while their caller used the result. */
    uint32_t uninitialized_operands; /**< Uninitialized locals and arguments read as Integer 0. */
    uint32_t debug_stores;           /**< Stores to Debug. */
    uint32_t mixed_type_comparisons; /**< Comparisons and Concatenates whose second operand was converted to the
                                          first's type (§5.2). */
    uint32_t forbidden_conversions;  /**< Implicit conversions to Integer that ACPI forbids: an empty String or
                                          Buffer, a 0x prefix (§5.2). */
    uint32_t references_built;       /**< Index results that became references: those DerefOf reads at once are not
                                          (§7.2, O8). */
    uint32_t frame_serial;           /**< The serial of the last frame pushed. */
    uint32_t table_locals_serial;    /**< The serial every frame of the table whose locals table_locals holds shares,
                                          so that a reference to one of them lives from a statement to the next. */
    uint32_t table_locals_owner;     /**< The table, counted from 1, whose Local0 to Local7 table_locals holds. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t
        table_locals[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LOCALS]; /**< The locals of that
                                                                                              table's code, which
                                                                                              last from one of its
                                                                                              statements to the
                                                                                              next. */
    uint32_t frames_peak; /**< Most frames in use at once. */
    uint32_t values_peak; /**< Most values in use at once. */
    uint32_t statuses[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT];      /**< Failures, per
                                                                                                          status. */
    uint32_t unimplemented[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT]; /**< Opcodes that
                                                                                                          failed with
                                                                                                          unimplemented_
                                                                                                          operator. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t;

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_INTERPRETER_H */
