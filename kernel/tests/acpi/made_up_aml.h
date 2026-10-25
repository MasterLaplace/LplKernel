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
 * @file made_up_aml.h
 * @brief AML written term by term, for the tests whose tables are built at run time.
 *
 * The region tests patch the address of memory they own into a region, which a fixed array cannot
 * hold (docs/acpi/specification.md, §8.2): they write their AML with these calls, each appending
 * one term, its package lengths computed.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-25
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_MADE_UP_AML_H
#define KERNEL_TESTS_ACPI_MADE_UP_AML_H

#include <stdbool.h>
#include <stdint.h>

#define MADE_UP_AML_CAPACITY 2048u /**< Bytes of AML one made-up builder holds. */

/**
 * @brief AML being written, and whether it all fit.
 */
typedef struct MadeUpAml {
    uint8_t bytes[MADE_UP_AML_CAPACITY]; /**< The AML. */
    uint32_t length;                     /**< Bytes written. */
    bool overflowed;                     /**< Whether a term did not fit, and was dropped. */
} MadeUpAml_t;

/**
 * @brief One unit of a field list: a name of four characters, or NULL for a reserved run of bits.
 */
typedef struct MadeUpFieldUnit {
    const char *name; /**< Four characters, or NULL. */
    uint32_t bits;    /**< Its length in bits. */
} MadeUpFieldUnit_t;

/**
 * @brief Empties @p aml.
 *
 * @param aml The builder.
 */
extern void made_up_aml_begin(MadeUpAml_t *aml);

/**
 * @brief Appends the shortest encoding of an integer constant: Zero, One, Ones under 64 bits, or a
 *        prefixed byte, word, double word or quad word.
 *
 * @param aml   The builder.
 * @param value The constant.
 */
extern void made_up_aml_integer(MadeUpAml_t *aml, uint64_t value);

/**
 * @brief Appends a NameString of one NameSeg.
 *
 * @param aml  The builder.
 * @param name Four characters.
 */
extern void made_up_aml_name(MadeUpAml_t *aml, const char *name);

/**
 * @brief Appends Name (@p name, @p value).
 *
 * @param aml   The builder.
 * @param name  Four characters.
 * @param value An integer constant.
 */
extern void made_up_aml_name_integer(MadeUpAml_t *aml, const char *name, uint64_t value);

/**
 * @brief Appends OperationRegion (@p name, @p space, @p offset, @p length).
 *
 * @param aml    The builder.
 * @param name   Four characters.
 * @param space  The RegionSpace byte.
 * @param offset Its offset, an integer constant.
 * @param length Its length, an integer constant.
 */
extern void made_up_aml_region(MadeUpAml_t *aml, const char *name, uint8_t space, uint64_t offset, uint64_t length);

/**
 * @brief Appends Field (@p region, @p flags) { @p units }.
 *
 * @param aml    The builder.
 * @param region Four characters.
 * @param flags  The FieldFlags byte: AccessType, LockRule, UpdateRule.
 * @param units  The units, in order.
 * @param count  How many.
 */
extern void made_up_aml_field(MadeUpAml_t *aml, const char *region, uint8_t flags, const MadeUpFieldUnit_t *units,
                              uint32_t count);

/**
 * @brief Appends IndexField (@p index, @p data, @p flags) { @p units }.
 *
 * @param aml   The builder.
 * @param index Four characters: the index register's name.
 * @param data  Four characters: the data register's name.
 * @param flags The FieldFlags byte.
 * @param units The units, in order.
 * @param count How many.
 */
extern void made_up_aml_index_field(MadeUpAml_t *aml, const char *index, const char *data, uint8_t flags,
                                    const MadeUpFieldUnit_t *units, uint32_t count);

/**
 * @brief Appends BankField (@p region, @p bank, @p bank_value, @p flags) { @p units }.
 *
 * @param aml        The builder.
 * @param region     Four characters.
 * @param bank       Four characters: the bank register's name.
 * @param bank_value Its BankValue, an integer constant.
 * @param flags      The FieldFlags byte.
 * @param units      The units, in order.
 * @param count      How many.
 */
extern void made_up_aml_bank_field(MadeUpAml_t *aml, const char *region, const char *bank, uint64_t bank_value,
                                   uint8_t flags, const MadeUpFieldUnit_t *units, uint32_t count);

/**
 * @brief Appends Store (@p source, @p target), both names of one NameSeg.
 *
 * @param aml    The builder.
 * @param source Four characters.
 * @param target Four characters.
 */
extern void made_up_aml_store_name(MadeUpAml_t *aml, const char *source, const char *target);

/**
 * @brief Appends Store (@p value, @p target): an integer constant to a name of one NameSeg.
 *
 * @param aml    The builder.
 * @param value  An integer constant.
 * @param target Four characters.
 */
extern void made_up_aml_store_integer(MadeUpAml_t *aml, uint64_t value, const char *target);

/**
 * @brief Appends raw bytes, for a term the other calls do not write.
 *
 * @param aml    The builder.
 * @param bytes  The bytes.
 * @param length How many.
 */
extern void made_up_aml_raw(MadeUpAml_t *aml, const uint8_t *bytes, uint32_t length);

#endif /* KERNEL_TESTS_ACPI_MADE_UP_AML_H */
