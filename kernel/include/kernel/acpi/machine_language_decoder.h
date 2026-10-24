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
 * @file machine_language_decoder.h
 * @brief The AML decoder: opcode descriptors, PkgLength and NameString.
 *
 * Layer L1 of the specification (docs/acpi/specification.md, §3.1 and §4.2). Every encoding of the
 * byte table of ACPI 6.6 §20.3 has a descriptor, found by direct indexing; what the decoder reads,
 * it reads within a limit it is given, and a read that would pass it fails with a status instead.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_ACPI_MACHINE_LANGUAGE_DECODER_H
#define KERNEL_ACPI_MACHINE_LANGUAGE_DECODER_H

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_PREFIX                                    \
    0x5Bu /**< The byte before every two-byte opcode. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OPERANDS                                       \
    6u /**< Operands one descriptor lists, its PkgLength apart. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_BITS                                       \
    4u /**< Bits one operand kind takes in a descriptor's operands. */

/**
 * @brief Every one-byte opcode, as X(identifier, encoding, name, package length, statement, data
 *        object, opens a scope, then six operand kinds).
 *
 * @details The name is the byte table's, without "Op". A descriptor whose package length column is
 *          1 reads a PkgLength right after its opcode. A statement (a named object or a Type1Opcode)
 *          is refused where a TermArg is expected. A data object may be a package element. A
 *          descriptor that opens a scope runs its TermList in the scope of the name it declares or
 *          names. Each operand kind names an ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_*
 *          value; NONE fills the columns past the last operand.
 */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(X)                                \
    X(ZERO, 0x00, "Zero", 0, 0, 1, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                              \
    X(ONE, 0x01, "One", 0, 0, 1, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                                \
    X(ALIAS, 0x06, "Alias", 0, 1, 0, 0, NAME_STRING, NAME_STRING, NONE, NONE, NONE, NONE)                              \
    X(NAME, 0x08, "Name", 0, 1, 0, 0, NAME_STRING, TERM_ARGUMENT, NONE, NONE, NONE, NONE)                              \
    X(BYTE_PREFIX, 0x0A, "BytePrefix", 0, 0, 1, 0, BYTE_DATA, NONE, NONE, NONE, NONE, NONE)                            \
    X(WORD_PREFIX, 0x0B, "WordPrefix", 0, 0, 1, 0, WORD_DATA, NONE, NONE, NONE, NONE, NONE)                            \
    X(DOUBLE_WORD_PREFIX, 0x0C, "DWordPrefix", 0, 0, 1, 0, DOUBLE_WORD_DATA, NONE, NONE, NONE, NONE, NONE)             \
    X(STRING_PREFIX, 0x0D, "StringPrefix", 0, 0, 1, 0, ASCII_STRING, NONE, NONE, NONE, NONE, NONE)                     \
    X(QUAD_WORD_PREFIX, 0x0E, "QWordPrefix", 0, 0, 1, 0, QUAD_WORD_DATA, NONE, NONE, NONE, NONE, NONE)                 \
    X(SCOPE, 0x10, "Scope", 1, 1, 0, 1, NAME_STRING, TERM_LIST, NONE, NONE, NONE, NONE)                                \
    X(BUFFER, 0x11, "Buffer", 1, 0, 1, 0, TERM_ARGUMENT, BYTE_LIST, NONE, NONE, NONE, NONE)                            \
    X(PACKAGE, 0x12, "Package", 1, 0, 1, 0, BYTE_DATA, PACKAGE_ELEMENT_LIST, NONE, NONE, NONE, NONE)                   \
    X(VARIABLE_PACKAGE, 0x13, "VarPackage", 1, 0, 1, 0, TERM_ARGUMENT, PACKAGE_ELEMENT_LIST, NONE, NONE, NONE, NONE)   \
    X(METHOD, 0x14, "Method", 1, 1, 0, 1, NAME_STRING, BYTE_DATA, TERM_LIST, NONE, NONE, NONE)                         \
    X(EXTERNAL, 0x15, "External", 0, 1, 0, 0, NAME_STRING, BYTE_DATA, BYTE_DATA, NONE, NONE, NONE)                     \
    X(LOCAL0, 0x60, "Local0", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                          \
    X(LOCAL1, 0x61, "Local1", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                          \
    X(LOCAL2, 0x62, "Local2", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                          \
    X(LOCAL3, 0x63, "Local3", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                          \
    X(LOCAL4, 0x64, "Local4", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                          \
    X(LOCAL5, 0x65, "Local5", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                          \
    X(LOCAL6, 0x66, "Local6", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                          \
    X(LOCAL7, 0x67, "Local7", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                          \
    X(ARGUMENT0, 0x68, "Arg0", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                         \
    X(ARGUMENT1, 0x69, "Arg1", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                         \
    X(ARGUMENT2, 0x6A, "Arg2", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                         \
    X(ARGUMENT3, 0x6B, "Arg3", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                         \
    X(ARGUMENT4, 0x6C, "Arg4", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                         \
    X(ARGUMENT5, 0x6D, "Arg5", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                         \
    X(ARGUMENT6, 0x6E, "Arg6", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                         \
    X(STORE, 0x70, "Store", 0, 0, 0, 0, TERM_ARGUMENT, SUPER_NAME, NONE, NONE, NONE, NONE)                             \
    X(REFERENCE_OF, 0x71, "RefOf", 0, 0, 0, 0, SIMPLE_NAME, NONE, NONE, NONE, NONE, NONE)                              \
    X(ADD, 0x72, "Add", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                            \
    X(CONCATENATE, 0x73, "Concat", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                 \
    X(SUBTRACT, 0x74, "Subtract", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                  \
    X(INCREMENT, 0x75, "Increment", 0, 0, 0, 0, SUPER_NAME, NONE, NONE, NONE, NONE, NONE)                              \
    X(DECREMENT, 0x76, "Decrement", 0, 0, 0, 0, SUPER_NAME, NONE, NONE, NONE, NONE, NONE)                              \
    X(MULTIPLY, 0x77, "Multiply", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                  \
    X(DIVIDE, 0x78, "Divide", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, TARGET, NONE, NONE)                    \
    X(SHIFT_LEFT, 0x79, "ShiftLeft", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)               \
    X(SHIFT_RIGHT, 0x7A, "ShiftRight", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)             \
    X(AND, 0x7B, "And", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                            \
    X(NAND, 0x7C, "Nand", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                          \
    X(OR, 0x7D, "Or", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                              \
    X(NOR, 0x7E, "Nor", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                            \
    X(XOR, 0x7F, "Xor", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                            \
    X(NOT, 0x80, "Not", 0, 0, 0, 0, TERM_ARGUMENT, TARGET, NONE, NONE, NONE, NONE)                                     \
    X(FIND_SET_LEFT_BIT, 0x81, "FindSetLeftBit", 0, 0, 0, 0, TERM_ARGUMENT, TARGET, NONE, NONE, NONE, NONE)            \
    X(FIND_SET_RIGHT_BIT, 0x82, "FindSetRightBit", 0, 0, 0, 0, TERM_ARGUMENT, TARGET, NONE, NONE, NONE, NONE)          \
    X(DEREFERENCE_OF, 0x83, "DerefOf", 0, 0, 0, 0, TERM_ARGUMENT, NONE, NONE, NONE, NONE, NONE)                        \
    X(CONCATENATE_RESOURCE_TEMPLATE, 0x84, "ConcatRes", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE,  \
      NONE)                                                                                                            \
    X(MOD, 0x85, "Mod", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                            \
    X(NOTIFY, 0x86, "Notify", 0, 1, 0, 0, SUPER_NAME, TERM_ARGUMENT, NONE, NONE, NONE, NONE)                           \
    X(SIZE_OF, 0x87, "SizeOf", 0, 0, 0, 0, SUPER_NAME, NONE, NONE, NONE, NONE, NONE)                                   \
    X(INDEX, 0x88, "Index", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                        \
    X(MATCH, 0x89, "Match", 0, 0, 0, 0, TERM_ARGUMENT, BYTE_DATA, TERM_ARGUMENT, BYTE_DATA, TERM_ARGUMENT,             \
      TERM_ARGUMENT)                                                                                                   \
    X(CREATE_DOUBLE_WORD_FIELD, 0x8A, "CreateDWordField", 0, 1, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NAME_STRING, NONE, \
      NONE, NONE)                                                                                                      \
    X(CREATE_WORD_FIELD, 0x8B, "CreateWordField", 0, 1, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NAME_STRING, NONE, NONE,   \
      NONE)                                                                                                            \
    X(CREATE_BYTE_FIELD, 0x8C, "CreateByteField", 0, 1, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NAME_STRING, NONE, NONE,   \
      NONE)                                                                                                            \
    X(CREATE_BIT_FIELD, 0x8D, "CreateBitField", 0, 1, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NAME_STRING, NONE, NONE,     \
      NONE)                                                                                                            \
    X(OBJECT_TYPE, 0x8E, "ObjectType", 0, 0, 0, 0, SIMPLE_NAME, NONE, NONE, NONE, NONE, NONE)                          \
    X(CREATE_QUAD_WORD_FIELD, 0x8F, "CreateQWordField", 0, 1, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NAME_STRING, NONE,   \
      NONE, NONE)                                                                                                      \
    X(LOGICAL_AND, 0x90, "LAnd", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NONE, NONE, NONE, NONE)                     \
    X(LOGICAL_OR, 0x91, "LOr", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NONE, NONE, NONE, NONE)                       \
    X(LOGICAL_NOT, 0x92, "LNot", 0, 0, 0, 0, TERM_ARGUMENT, NONE, NONE, NONE, NONE, NONE)                              \
    X(LOGICAL_EQUAL, 0x93, "LEqual", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NONE, NONE, NONE, NONE)                 \
    X(LOGICAL_GREATER, 0x94, "LGreater", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NONE, NONE, NONE, NONE)             \
    X(LOGICAL_LESS, 0x95, "LLess", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, NONE, NONE, NONE, NONE)                   \
    X(TO_BUFFER, 0x96, "ToBuffer", 0, 0, 0, 0, TERM_ARGUMENT, TARGET, NONE, NONE, NONE, NONE)                          \
    X(TO_DECIMAL_STRING, 0x97, "ToDecimalString", 0, 0, 0, 0, TERM_ARGUMENT, TARGET, NONE, NONE, NONE, NONE)           \
    X(TO_HEX_STRING, 0x98, "ToHexString", 0, 0, 0, 0, TERM_ARGUMENT, TARGET, NONE, NONE, NONE, NONE)                   \
    X(TO_INTEGER, 0x99, "ToInteger", 0, 0, 0, 0, TERM_ARGUMENT, TARGET, NONE, NONE, NONE, NONE)                        \
    X(TO_STRING, 0x9C, "ToString", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE, NONE)                 \
    X(COPY_OBJECT, 0x9D, "CopyObject", 0, 0, 0, 0, TERM_ARGUMENT, SIMPLE_NAME, NONE, NONE, NONE, NONE)                 \
    X(MID, 0x9E, "Mid", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TERM_ARGUMENT, TARGET, NONE, NONE)                   \
    X(CONTINUE, 0x9F, "Continue", 0, 1, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                      \
    X(IF, 0xA0, "If", 1, 1, 0, 0, TERM_ARGUMENT, TERM_LIST, NONE, NONE, NONE, NONE)                                    \
    X(ELSE, 0xA1, "Else", 1, 1, 0, 0, TERM_LIST, NONE, NONE, NONE, NONE, NONE)                                         \
    X(WHILE, 0xA2, "While", 1, 1, 0, 0, TERM_ARGUMENT, TERM_LIST, NONE, NONE, NONE, NONE)                              \
    X(NOOP, 0xA3, "Noop", 0, 1, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                              \
    X(RETURN, 0xA4, "Return", 0, 1, 0, 0, TERM_ARGUMENT, NONE, NONE, NONE, NONE, NONE)                                 \
    X(BREAK, 0xA5, "Break", 0, 1, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                            \
    X(BREAK_POINT, 0xCC, "BreakPoint", 0, 1, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                 \
    X(ONES, 0xFF, "Ones", 0, 0, 1, 0, NONE, NONE, NONE, NONE, NONE, NONE)

/**
 * @brief Every opcode that follows the extended prefix 0x5B, in the columns of
 *        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES; the encoding is the
 *        second byte.
 *
 * @details Processor (permanently reserved, ACPI 6.6 Appendix C) and Unload (reserved) are decoded:
 *          firmware still ships them.
 */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_OPCODES(X)                                \
    X(MUTEX, 0x01, "Mutex", 0, 1, 0, 0, NAME_STRING, BYTE_DATA, NONE, NONE, NONE, NONE)                                \
    X(EVENT, 0x02, "Event", 0, 1, 0, 0, NAME_STRING, NONE, NONE, NONE, NONE, NONE)                                     \
    X(CONDITIONAL_REFERENCE_OF, 0x12, "CondRefOf", 0, 0, 0, 0, SIMPLE_NAME, TARGET, NONE, NONE, NONE, NONE)            \
    X(CREATE_FIELD, 0x13, "CreateField", 0, 1, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TERM_ARGUMENT, NAME_STRING, NONE,   \
      NONE)                                                                                                            \
    X(LOAD_TABLE, 0x1F, "LoadTable", 0, 0, 0, 0, TERM_ARGUMENT, TERM_ARGUMENT, TERM_ARGUMENT, TERM_ARGUMENT,           \
      TERM_ARGUMENT, TERM_ARGUMENT)                                                                                    \
    X(LOAD, 0x20, "Load", 0, 0, 0, 0, SUPER_NAME, TARGET, NONE, NONE, NONE, NONE)                                      \
    X(STALL, 0x21, "Stall", 0, 1, 0, 0, TERM_ARGUMENT, NONE, NONE, NONE, NONE, NONE)                                   \
    X(SLEEP, 0x22, "Sleep", 0, 1, 0, 0, TERM_ARGUMENT, NONE, NONE, NONE, NONE, NONE)                                   \
    X(ACQUIRE, 0x23, "Acquire", 0, 0, 0, 0, SUPER_NAME, WORD_DATA, NONE, NONE, NONE, NONE)                             \
    X(SIGNAL, 0x24, "Signal", 0, 1, 0, 0, SUPER_NAME, NONE, NONE, NONE, NONE, NONE)                                    \
    X(WAIT, 0x25, "Wait", 0, 0, 0, 0, SUPER_NAME, TERM_ARGUMENT, NONE, NONE, NONE, NONE)                               \
    X(RESET, 0x26, "Reset", 0, 1, 0, 0, SUPER_NAME, NONE, NONE, NONE, NONE, NONE)                                      \
    X(RELEASE, 0x27, "Release", 0, 1, 0, 0, SUPER_NAME, NONE, NONE, NONE, NONE, NONE)                                  \
    X(FROM_BINARY_CODED_DECIMAL, 0x28, "FromBCD", 0, 0, 0, 0, TERM_ARGUMENT, TARGET, NONE, NONE, NONE, NONE)           \
    X(TO_BINARY_CODED_DECIMAL, 0x29, "ToBCD", 0, 0, 0, 0, TERM_ARGUMENT, TARGET, NONE, NONE, NONE, NONE)               \
    X(UNLOAD, 0x2A, "Unload", 0, 1, 0, 0, SUPER_NAME, NONE, NONE, NONE, NONE, NONE)                                    \
    X(REVISION, 0x30, "Revision", 0, 0, 1, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                      \
    X(DEBUG, 0x31, "Debug", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                            \
    X(FATAL, 0x32, "Fatal", 0, 1, 0, 0, BYTE_DATA, DOUBLE_WORD_DATA, TERM_ARGUMENT, NONE, NONE, NONE)                  \
    X(TIMER, 0x33, "Timer", 0, 0, 0, 0, NONE, NONE, NONE, NONE, NONE, NONE)                                            \
    X(OPERATION_REGION, 0x80, "OperationRegion", 0, 1, 0, 0, NAME_STRING, BYTE_DATA, TERM_ARGUMENT, TERM_ARGUMENT,     \
      NONE, NONE)                                                                                                      \
    X(FIELD, 0x81, "Field", 1, 1, 0, 0, NAME_STRING, BYTE_DATA, FIELD_LIST, NONE, NONE, NONE)                          \
    X(DEVICE, 0x82, "Device", 1, 1, 0, 1, NAME_STRING, TERM_LIST, NONE, NONE, NONE, NONE)                              \
    X(PROCESSOR, 0x83, "Processor", 1, 1, 0, 1, NAME_STRING, BYTE_DATA, DOUBLE_WORD_DATA, BYTE_DATA, TERM_LIST, NONE)  \
    X(POWER_RESOURCE, 0x84, "PowerResource", 1, 1, 0, 1, NAME_STRING, BYTE_DATA, WORD_DATA, TERM_LIST, NONE, NONE)     \
    X(THERMAL_ZONE, 0x85, "ThermalZone", 1, 1, 0, 1, NAME_STRING, TERM_LIST, NONE, NONE, NONE, NONE)                   \
    X(INDEX_FIELD, 0x86, "IndexField", 1, 1, 0, 0, NAME_STRING, NAME_STRING, BYTE_DATA, FIELD_LIST, NONE, NONE)        \
    X(BANK_FIELD, 0x87, "BankField", 1, 1, 0, 0, NAME_STRING, NAME_STRING, TERM_ARGUMENT, BYTE_DATA, FIELD_LIST, NONE) \
    X(DATA_REGION, 0x88, "DataRegion", 0, 1, 0, 0, NAME_STRING, TERM_ARGUMENT, TERM_ARGUMENT, TERM_ARGUMENT, NONE, NONE)

/**
 * @brief What an operand of a descriptor is, and so how the decoder reads it.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_NONE = 0, /**< No operand: the list ends. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_TERM_ARGUMENT, /**< An expression, a local, an
                                                                                           argument, a constant, or a
                                                                                           name, which may be a call. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_SUPER_NAME,    /**< A name (a method there is
                                                                                           called), a local, an argument,
                                                                                           Debug, or a reference. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_TARGET,        /**< A SuperName, or NullName. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_SIMPLE_NAME,   /**< A SuperName whose name is
                                                                                           never called. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_NAME_STRING,   /**< A name, never resolved as a
                                                                                           call. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_BYTE_DATA,     /**< One byte. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_WORD_DATA,     /**< Two bytes, little-endian. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_DOUBLE_WORD_DATA, /**< Four bytes. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_QUAD_WORD_DATA,   /**< Eight bytes. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_ASCII_STRING, /**< Characters up to a NUL. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_BYTE_LIST,  /**< Every byte up to the PkgLength
                                                                                        end. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_FIELD_LIST, /**< Field elements up to the
                                                                                        PkgLength end. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_PACKAGE_ELEMENT_LIST, /**< Data objects and
                                                                                                  names up to the
                                                                                                  PkgLength end. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_TERM_LIST, /**< Terms up to the PkgLength end.
                                                                                    */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t;

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ENUMERATOR(identifier, ...)                 \
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##identifier, /**< One descriptor. */

/**
 * @brief The index of each descriptor: one per opcode, 0 for a byte that is no opcode.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageOpcode {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INVALID = 0, /**< No opcode. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ENUMERATOR)
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_OPCODES(
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ENUMERATOR)
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT /**< Descriptors, the invalid one
                                                                                     included. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageOpcode_t;

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_PACKAGE_LENGTH                                \
    0x01u /**< A PkgLength follows the opcode. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_STATEMENT                                     \
    0x02u /**< Refused where a TermArg is expected: a named object or a Type1Opcode. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_DATA_OBJECT                                   \
    0x04u /**< Allowed as a package element. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_OPENS_SCOPE                                   \
    0x08u /**< Its TermList runs in the scope of the name it declares or names. */

/**
 * @brief How one opcode is encoded and what follows it.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor {
    uint32_t operands;     /**< Operand kinds, 4 bits each, the first in the lowest bits. */
    uint16_t encoding;     /**< The opcode byte, or 0x5B00 plus the second byte of an extended opcode. */
    uint8_t operand_count; /**< Operands listed, its PkgLength apart. */
    uint8_t flags;         /**< ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_* bits. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t;

/**
 * @brief Why decoding or loading AML failed; the names are those of the specification, §6.2.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK = 0,    /**< Nothing failed. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED, /**< An operand passes its limit, or a
                                                                                      NameString is malformed. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_PACKAGE_LENGTH_OUT_OF_PARENT, /**< A PkgLength
                                                                                                         ends past the
                                                                                                         construct
                                                                                                         around it. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INVALID_OPCODE, /**< A byte that is no opcode, or
                                                                                           an opcode where its kind is
                                                                                           not allowed. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND, /**< A name that must exist does
                                                                                           not, or climbs above the
                                                                                           root. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY,  /**< The host gave no pages, or
                                                                                           the arena cap is reached. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_NAMESPACE_NODES, /**< The node cap is
                                                                                                  reached. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BLOCKS, /**< Constructs nest deeper than
                                                                                         the block stack holds. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PENDING_OPERATORS, /**< Operators nest
                                                                                                    deeper than the
                                                                                                    operator stack
                                                                                                    holds. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_TEMPORARY_NODES,   /**< Methods create more
                                                                                                    nodes than the
                                                                                                    temporary stack
                                                                                                    holds. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_FRAMES, /**< Method bodies nest deeper than
                                                                                         the frame limit. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT,        /**< Statuses. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t;

/**
 * @brief A NameString as it sits in the AML: its prefix, and where its segments start.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString {
    uint32_t segments;       /**< Offset of the first NameSeg, 4 bytes each, in the bytes it was read from. */
    uint8_t segment_count;   /**< NameSegs: 0 for NullName, up to 255. */
    uint8_t parent_prefixes; /**< `^` characters before the path. */
    bool root;               /**< Whether it starts at the root, with `\`. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t;

/**
 * @brief The descriptor at an index.
 *
 * @param opcode A descriptor index; ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INVALID
 *               and indices past the last give the invalid descriptor, which has no operand.
 * @return The descriptor, never NULL.
 */
extern const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t *
advanced_configuration_and_power_interface_machine_language_descriptor(uint32_t opcode);

/**
 * @brief The byte table's name of an opcode, without "Op".
 *
 * @param opcode A descriptor index.
 * @return The name, or "Invalid" for the invalid descriptor and indices past the last.
 */
extern const char *advanced_configuration_and_power_interface_machine_language_opcode_name(uint32_t opcode);

/**
 * @brief The text of a status, as its counter is named.
 *
 * @param status A status.
 * @return The text, such as "pkglength_out_of_parent", or "unknown" past the last status.
 */
extern const char *advanced_configuration_and_power_interface_machine_language_status_name(uint32_t status);

/**
 * @brief Operand kind number @p index of a descriptor.
 *
 * @param descriptor The descriptor.
 * @param index      The operand's position, from 0.
 * @return Its kind, or ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_NONE past the last.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t
advanced_configuration_and_power_interface_machine_language_operand_kind(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t *descriptor, uint32_t index);

/**
 * @brief Tells whether a byte starts a NameString: `\`, `^`, a dual or multi prefix, or the first
 *        character of a NameSeg.
 *
 * @param byte The byte.
 * @return true when a NameString starts with it.
 */
extern bool advanced_configuration_and_power_interface_machine_language_starts_name(uint8_t byte);

/**
 * @brief Reads the opcode at @p offset.
 *
 * @param bytes     The AML.
 * @param offset    Where the opcode starts.
 * @param limit     The first offset it may not read.
 * @param out_opcode Receives the descriptor index.
 * @param out_after  Receives the offset after the opcode.
 * @return TRUNCATED when the opcode passes @p limit; INVALID_OPCODE when the bytes are no opcode,
 *         a NameString's first byte included; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_opcode(const uint8_t *bytes, uint32_t offset,
                                                                        uint32_t limit, uint32_t *out_opcode,
                                                                        uint32_t *out_after);

/**
 * @brief Reads the value of a PkgLength encoding: 1 to 4 bytes, bits 7-6 of the first counting the
 *        bytes that follow.
 *
 * @details A field element's bit length uses the same encoding, with no end to check.
 * @param bytes     The AML.
 * @param offset    Where the encoding starts.
 * @param limit     The first offset it may not read.
 * @param out_value Receives the value, below 2^28.
 * @param out_after Receives the offset after the encoding.
 * @return TRUNCATED when the encoding passes @p limit; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_package_length(const uint8_t *bytes, uint32_t offset,
                                                                                uint32_t limit, uint32_t *out_value,
                                                                                uint32_t *out_after);

/**
 * @brief Reads a PkgLength and gives the end of the construct it opens: the PkgLength counts its
 *        own bytes, so the end is @p offset plus its value.
 *
 * @param bytes     The AML.
 * @param offset    Where the PkgLength starts.
 * @param limit     The end of the construct around it.
 * @param out_end   Receives the end of the construct.
 * @param out_after Receives the offset after the encoding.
 * @return TRUNCATED when the encoding passes @p limit, or when the value is smaller than the
 *         encoding; PACKAGE_LENGTH_OUT_OF_PARENT when the end passes @p limit; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_package_end(const uint8_t *bytes, uint32_t offset,
                                                                             uint32_t limit, uint32_t *out_end,
                                                                             uint32_t *out_after);

/**
 * @brief Reads a NameString: `\` or any number of `^`, then NullName, one NameSeg, a dual path of
 *        two, or a multi path of 1 to 255.
 *
 * @param bytes     The AML.
 * @param offset    Where the NameString starts.
 * @param limit     The first offset it may not read.
 * @param out_name  Receives its prefix and where its segments start.
 * @param out_after Receives the offset after it.
 * @return TRUNCATED when it passes @p limit, when a multi path counts 0 segments, or when a NameSeg
 *         holds a character ACPI forbids there; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_name_string(
    const uint8_t *bytes, uint32_t offset, uint32_t limit,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t *out_name, uint32_t *out_after);

/**
 * @brief The NameSeg at @p offset as one integer, its first character in the lowest byte, so that two
 *        NameSegs compare as integers.
 *
 * @param bytes  The AML.
 * @param offset Where the NameSeg starts; the caller has checked its 4 bytes.
 * @return The NameSeg.
 */
extern uint32_t advanced_configuration_and_power_interface_machine_language_name_segment(const uint8_t *bytes,
                                                                                         uint32_t offset);

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_DECODER_H */
