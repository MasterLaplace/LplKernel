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
 * @file machine_language_objects.h
 * @brief The objects AML values point at: strings, buffers, packages, regions and field units.
 *
 * Part of layer L3 of the specification (docs/acpi/specification.md, §3.4 and §3.5). An Integer
 * lives in its value; every other object is one allocation in the object heap, a 16-byte header
 * followed by its bytes or its element values, named by a handle that holds no pointer. The heap
 * takes its pages from the namespace arena, under the same cap.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-25
 **************************************************************************/

#ifndef KERNEL_ACPI_MACHINE_LANGUAGE_OBJECTS_H
#define KERNEL_ACPI_MACHINE_LANGUAGE_OBJECTS_H

#include <kernel/acpi/machine_language_namespace.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_RUN_PAGES                                   \
    16u /**< Pages of one run of small objects: 64 KiB. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OBJECT_RUNS                                    \
    256u /**< Runs one heap holds: runs of small objects, and one run per large object. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LARGE_OBJECT_BYTES                                 \
    4096u /**< An object this large or larger gets pages of its own. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES                                   \
    (64u * 1024u) /**< The largest string or buffer (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_PACKAGE_ELEMENTS                               \
    255u /**< Elements of a Package (ACPI 6.6 §19.3.5). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VARIABLE_PACKAGE_ELEMENTS                      \
    4096u /**< Elements of a VarPackage (§6.1). */

/**
 * @brief What a value is: the ACPI ObjectType numbers (ACPI 6.6 §19.6.97) for the data objects,
 *        then the kinds of value that are not data.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageValueKind {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_UNINITIALIZED = 0,     /**< No value. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER = 1,           /**< payload. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING = 2,            /**< handle: its bytes. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER = 3,            /**< handle: its bytes. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE = 4,           /**< handle: its values. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_NAME_REFERENCE = 0x80, /**< A name a package
                                                                                               element holds, resolved
                                                                                               when read: auxiliary is
                                                                                               its table, counted from
                                                                                               1, handle the scope it
                                                                                               was read in, payload its
                                                                                               offset. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION = 0x81,         /**< handle: a region. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_FIELD_UNIT = 0x82,   /**< handle: a field unit. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD = 0x83, /**< A buffer field: handle
                                                                                             is the node of its buffer,
                                                                                             payload its bit offset
                                                                                             (low half) and length
                                                                                             (high half), auxiliary its
                                                                                             access width in bytes. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageValueKind_t;

/**
 * @brief The header every object of the heap starts with (§3.4).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader {
    uint32_t references; /**< Counted references, saturating at 0xFFFFFFFF. */
    uint8_t kind;        /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_* kind. */
    uint8_t flags;       /**< Bits the kind defines. */
    uint16_t auxiliary;  /**< A small field the kind defines. */
    uint32_t length;     /**< Bytes of a string or buffer, values of a package, bytes of anything else. */
    uint32_t reserved;   /**< 0, so that what follows is 8-byte aligned on both targets. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t;

/**
 * @brief An operation region (§3.4): its space, and its offset and length once evaluated.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion {
    uint8_t space;         /**< RegionSpace, as AML encodes it. */
    uint8_t state;         /**< 1 once its offset and length are evaluated, 0 while they wait for the interpreter. */
    uint16_t reserved;     /**< 0. */
    uint32_t pci_device;   /**< Segment, bus, device and function, once attached; 0 before. */
    uint64_t offset;       /**< Its address in its space. */
    uint64_t length;       /**< Its length in bytes. */
    uint32_t mapping;      /**< Its entry in the mapping cache, once attached; 0 before. */
    uint32_t owner_device; /**< The device it is declared in, or 0. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t;

/**
 * @brief A unit of a Field, an IndexField or a BankField (§3.4).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit {
    uint8_t kind;          /**< The opcode that declared it: Field, IndexField or BankField, as a descriptor index. */
    uint8_t access;        /**< AccessType, from the FieldFlags or the last AccessAs before it. */
    uint8_t update;        /**< UpdateRule. */
    uint8_t lock;          /**< LockRule. */
    uint32_t region;       /**< The region's node, or the index field's for an IndexField. */
    uint32_t data_or_bank; /**< The data field's node for an IndexField, the bank field's for a BankField, else 0. */
    uint32_t bank_value;   /**< The BankValue of a BankField. */
    uint32_t bit_offset;   /**< Its first bit in the region. */
    uint32_t bit_length;   /**< Its length in bits. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t;

/**
 * @brief The object heap: runs of small objects, bump-allocated, and one run per large object.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap {
    void *runs[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OBJECT_RUNS]; /**< Each run's pages. */
    uint16_t run_pages[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OBJECT_RUNS]; /**< And their
                                                                                                         count. */
    uint32_t run_count;                                                                              /**< Runs taken. */
    uint32_t small_run;          /**< The run small objects are taken from, counted from 1; 0 before the first. */
    uint32_t small_used;         /**< Bytes of that run already taken. */
    uint32_t objects;            /**< Objects allocated. */
    uint32_t object_bytes;       /**< Bytes they take, headers included. */
    uint32_t packages_padded;    /**< Packages with fewer initialisers than elements, padded with Uninitialized. */
    uint32_t packages_truncated; /**< Packages with more initialisers than elements, the extra ones dropped. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t;

/**
 * @brief Allocates an object of @p kind with @p payload_bytes after its header, all zero.
 *
 * @param heap          The heap.
 * @param space         The namespace whose arena gives the pages.
 * @param kind          An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_* kind.
 * @param length        What the header's length holds.
 * @param payload_bytes Bytes after the header.
 * @param out_handle    Receives the object's handle, never 0.
 * @return OUT_OF_MEMORY when the arena cap is reached, the host gives no pages, or the heap holds
 *         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OBJECT_RUNS runs; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_object_allocate(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint8_t kind, uint32_t length,
    uint32_t payload_bytes, uint32_t *out_handle);

/**
 * @brief The header of the object a handle names.
 *
 * @param heap   The heap.
 * @param handle A handle the heap gave.
 * @return The header, or NULL for 0 or a handle the heap did not give.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *
advanced_configuration_and_power_interface_machine_language_object(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap, uint32_t handle);

/**
 * @brief Where a data object is read from, and with what rules: its AML, the scope its names are
 *        read in, its table, and the integer width.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageSource {
    const uint8_t *bytes; /**< The table, header included. */
    uint32_t scope;       /**< The node names are read from. */
    uint16_t table;       /**< The table, counted from 1. */
    uint8_t integer_bits; /**< 32 or 64: Ones and every integer are masked to it. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t;

/**
 * @brief Builds the value of a data object that needs no interpreter: an integer constant, a String,
 *        a Buffer whose size is an integer constant, or a Package or VarPackage of such objects and
 *        of names, which stay names until read (§5.1).
 *
 * @details A Package with fewer initialisers than elements is padded with Uninitialized, one with
 *          more keeps the first; packages nest up to 32 deep.
 * @param heap         The heap the objects go to.
 * @param space        The namespace whose arena gives the pages.
 * @param source       Where the object is read from.
 * @param offset       Where it starts.
 * @param limit        The first offset it may not reach.
 * @param out_value    Receives the value, Uninitialized when the object needs the interpreter.
 * @param out_constant Receives whether it needed none.
 * @return TRUNCATED, PACKAGE_LENGTH_OUT_OF_PARENT or INVALID_OPCODE when the bytes are malformed;
 *         OUT_OF_MEMORY when the heap cannot hold the object, or LIMIT_PACKAGE_ELEMENTS and
 *         LIMIT_BUFFER_BYTES past the caps of §6.1; OK otherwise, the object needing the
 *         interpreter or not.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_evaluate_data(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t *source, uint32_t offset, uint32_t limit,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value, bool *out_constant);

/**
 * @brief Reads an integer that needs no interpreter: an integer constant, or a name whose node
 *        holds an Integer.
 *
 * @param space        The namespace names are resolved in.
 * @param source       Where the integer is read from.
 * @param offset       Where it starts.
 * @param limit        The first offset it may not reach.
 * @param out_integer  Receives the integer, masked to the integer width.
 * @param out_after    Receives the offset after the term.
 * @param out_constant Receives whether it needed no interpreter; when false, @p out_after is not set.
 * @return TRUNCATED or INVALID_OPCODE when the bytes are malformed; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_evaluate_integer(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t *source, uint32_t offset, uint32_t limit,
    uint64_t *out_integer, uint32_t *out_after, bool *out_constant);

/**
 * @brief Gives every page of a heap back to the namespace arena, and empties it.
 *
 * @param heap  The heap; its objects must not be used afterwards.
 * @param space The namespace whose arena gave the pages.
 */
extern void advanced_configuration_and_power_interface_machine_language_heap_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space);

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_OBJECTS_H */
