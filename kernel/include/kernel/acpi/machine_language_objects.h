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
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BUFFER_FIELD_READS_AS_BUFFER                       \
    0x01u /**< Value flag of a buffer field CreateField made: it reads as a Buffer at any length (§5.2). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_VISITED                                     \
    0x40u /**< Header flag the walk that looks for a reference cycle sets on a Package it met, and clears after. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_SIZE_CLASSES                                \
    32u /**< Size classes of small objects, headers included: every 16 bytes up to 256, then four per power of two     \
           up to 4,096. */

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
                                                                                             is the Buffer it was made
                                                                                             over, which it holds a
                                                                                             reference on, payload its
                                                                                             bit offset (low half) and
                                                                                             length (high half),
                                                                                             auxiliary its access width
                                                                                             in bytes. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_NODE = 0x84, /**< A reference to a named
                                                                                               object: handle is its
                                                                                               node. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_LOCAL = 0x85,    /**< A reference to a
                                                                                                   LocalX: handle is X,
                                                                                                   payload the frame's
                                                                                                   index (low half) and
                                                                                                   serial (high half). */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_ARGUMENT = 0x86, /**< A reference to an
                                                                                                   ArgX, laid out as a
                                                                                                   LocalX's. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_ELEMENT = 0x87, /**< Index of a Package:
                                                                                                  handle is the
                                                                                                  Package, which it
                                                                                                  holds a reference
                                                                                                  on, payload the
                                                                                                  element. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_BYTE = 0x88, /**< Index of a Buffer or a
                                                                                               String: handle is the
                                                                                               object, which it holds
                                                                                               a reference on, payload
                                                                                               the byte. */
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
 * @brief Which kind of field list a unit belongs to (§3.4).
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnitKind {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_NORMAL = 0, /**< A Field's: in its region. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_INDEX = 1,  /**< An IndexField's: through
                                                                                           an index and a data
                                                                                           field. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_BANK = 2,   /**< A BankField's: in its
                                                                                           region once a bank field
                                                                                           holds its BankValue. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnitKind_t;

/**
 * @brief A unit of a Field, an IndexField or a BankField (§3.4).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit {
    uint8_t kind;          /**< Normal, index or bank: an
                                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_* kind. */
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
 * @brief The object heap (§3.5): runs of small objects cut into blocks, each of the size class its
 *        object rounds up to, a freed block kept for the next object of its class, and one run per
 *        large object, whose pages go back when it is freed.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap {
    void *runs[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OBJECT_RUNS]; /**< Each run's pages,
                                                                                                 NULL once a large
                                                                                                 object gave them
                                                                                                 back. */
    uint16_t run_pages[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OBJECT_RUNS]; /**< And their
                                                                                                         count. */
    uint32_t run_count;  /**< Runs taken, those given back included: the next new one's index. */
    uint32_t small_run;  /**< The run new blocks are cut from, counted from 1; 0 before the first. */
    uint32_t small_used; /**< Bytes of that run already cut. */
    uint32_t free_blocks[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_SIZE_CLASSES]; /**< Per size
                                                                                                               class,
                                                                                                               the
                                                                                                               handle
                                                                                                               of the
                                                                                                               block
                                                                                                               freed
                                                                                                               last, 0
                                                                                                               when
                                                                                                               none. */
    uint32_t objects;            /**< Objects allocated. */
    uint32_t objects_freed;      /**< Of those, the ones freed: objects - objects_freed are alive. */
    uint32_t stale_handles;      /**< Retains and releases of an object already freed, dying or left with no
                                      reference: each is a defect, and changes nothing. A release whose block
                                      another object took meanwhile is not seen. */
    uint32_t object_bytes;       /**< Bytes the live objects' blocks take, headers included. */
    uint32_t object_bytes_peak;  /**< The most object_bytes reached. */
    uint32_t packages_padded;    /**< Packages with fewer initialisers than elements, padded with Uninitialized. */
    uint32_t packages_truncated; /**< Packages with more initialisers than elements, the extra ones dropped. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t;

/**
 * @brief Allocates an object of @p kind, all zero after its header, with one reference: its
 *        caller's.
 *
 * @details The bytes after the header follow from @p kind and @p length: a String's characters and
 *          its NUL, a Buffer's bytes, a Package's element values, Uninitialized, and @p length bytes
 *          for any other kind.
 * @param heap       The heap.
 * @param space      The namespace whose arena gives the pages.
 * @param kind       An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_* kind.
 * @param length     What the header's length holds: characters, bytes or elements.
 * @param out_handle Receives the object's handle, never 0.
 * @return LIMIT_BUFFER_BYTES when the object would pass the largest one (§6.1); OUT_OF_MEMORY when the
 *         arena cap is reached or the host gives no pages; LIMIT_OBJECT_RUNS when a new run would
 *         pass ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OBJECT_RUNS; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_object_allocate(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint8_t kind, uint32_t length,
    uint32_t *out_handle);

/**
 * @brief Takes one more reference on an object; a count that reaches 0xFFFFFFFF stays there (§3.5).
 *
 * @param heap   The heap.
 * @param handle A live object's handle.
 */
extern void advanced_configuration_and_power_interface_machine_language_object_retain(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap, uint32_t handle);

/**
 * @brief Gives back one reference on an object, and frees it when none is left: the objects a
 *        Package holds lose its references in turn, without C recursion. A saturated count never
 *        moves, and its object is never freed.
 *
 * @param heap   The heap.
 * @param space  The namespace whose arena gave the pages.
 * @param handle A live object's handle.
 */
extern void advanced_configuration_and_power_interface_machine_language_object_release(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t handle);

/**
 * @brief Whether a value of @p kind holds a reference on an object of the heap.
 *
 * @param kind An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_* kind.
 * @return true for a String, a Buffer, a Package, a region, a field unit, a buffer field, and an Index
 *         of a Package, a Buffer or a String.
 */
extern bool advanced_configuration_and_power_interface_machine_language_value_holds_object(uint8_t kind);

/**
 * @brief Takes one more reference on the object a value names, if it names one.
 *
 * @param heap  The heap.
 * @param value The value.
 */
extern void advanced_configuration_and_power_interface_machine_language_value_retain(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value);

/**
 * @brief Gives back the reference a value holds, if it holds one, and leaves it Uninitialized.
 *
 * @param heap  The heap.
 * @param space The namespace whose arena gave the pages.
 * @param value The value.
 */
extern void advanced_configuration_and_power_interface_machine_language_value_release(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value);

/**
 * @brief Allocates a String, a Buffer or a Package, zero-filled, and gives the value that names it.
 *
 * @param heap      The heap.
 * @param space     The namespace whose arena gives the pages.
 * @param kind      STRING, BUFFER or PACKAGE.
 * @param length    Its characters, bytes or elements.
 * @param out_value Receives the value, which holds the object's only reference.
 * @return What advanced_configuration_and_power_interface_machine_language_object_allocate returns.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_value_allocate(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint8_t kind, uint32_t length,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value);

/**
 * @brief The bytes that follow an object's header: a String's characters, a Buffer's bytes, a
 *        Package's element values.
 *
 * @param heap   The heap.
 * @param handle A live object's handle.
 * @return The first byte, or NULL for a handle the heap did not give.
 */
extern uint8_t *advanced_configuration_and_power_interface_machine_language_object_payload(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap, uint32_t handle);

/**
 * @brief The bytes a computational value holds: an Integer's 4 or 8 little-endian bytes, a String's
 *        characters without their NUL, a Buffer's bytes.
 *
 * @param heap         The heap.
 * @param value        The value.
 * @param integer_bits 32 or 64.
 * @param scratch      8 bytes an Integer's bytes are written in; @p out_bytes may point into it.
 * @param out_bytes    Receives the first byte.
 * @param out_length   Receives how many.
 * @return TYPE_MISMATCH for any other kind of value; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_value_bytes(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value, uint8_t integer_bits, uint8_t scratch[8],
    const uint8_t **out_bytes, uint32_t *out_length);

/**
 * @brief Converts an operand to the Integer an operator needs (§5.2): a Buffer gives its first 4 or 8
 *        bytes, little-endian; a String is read as hexadecimal up to its first non-hexadecimal
 *        character, a 0x prefix accepted; an empty String or Buffer gives 0.
 *
 * @param heap          The heap.
 * @param value         The operand.
 * @param integer_bits  32 or 64: the result is masked to it.
 * @param out_integer   Receives the Integer.
 * @param out_forbidden Receives whether ACPI forbids the conversion: an empty String or Buffer, a 0x
 *                      prefix.
 * @return TYPE_MISMATCH for a value that is no Integer, String or Buffer; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_value_to_integer(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value, uint8_t integer_bits,
    uint64_t *out_integer, bool *out_forbidden);

/**
 * @brief Converts a value as ToInteger does: a String is read past leading white space and a sign, in
 *        hexadecimal after 0x, in octal after a leading 0, in decimal otherwise, up to its first
 *        character that is no digit of its base, and saturates at Ones; a Buffer gives its first 4
 *        or 8 bytes.
 *
 * @param heap         The heap.
 * @param value        The value.
 * @param integer_bits 32 or 64: the result is masked to it.
 * @param out_integer  Receives the Integer.
 * @return TYPE_MISMATCH for a value that is no Integer, String or Buffer; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_value_to_integer_explicitly(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value, uint8_t integer_bits,
    uint64_t *out_integer);

/**
 * @brief How an Integer or a Buffer is written as a String.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceMachineLanguageStringForm {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_IMPLICIT = 0, /**< The implicit conversion, as
                                                                                         ACPICA writes it: an
                                                                                         Integer as 8 or 16
                                                                                         uppercase hexadecimal
                                                                                         digits, a Buffer as 0x and
                                                                                         two digits per byte,
                                                                                         separated by spaces. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_HEXADECIMAL,  /**< ToHexString: an Integer as 0x
                                                                                         and its digits, a Buffer as
                                                                                         0x and two digits per byte,
                                                                                         separated by commas. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_DECIMAL,      /**< ToDecimalString: an Integer
                                                                                         in decimal, a Buffer as its
                                                                                         bytes in decimal separated by
                                                                                         commas. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageStringForm_t;

/**
 * @brief Converts a value to a String in @p form; a String gives itself.
 *
 * @param heap         The heap.
 * @param space        The namespace whose arena gives the pages.
 * @param value        The value.
 * @param form         How an Integer or a Buffer is written.
 * @param integer_bits 32 or 64.
 * @param out_value    Receives the String, which holds a reference of its own.
 * @return TYPE_MISMATCH for a value that is no Integer, String or Buffer; what allocating returns;
 *         OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_value_to_string(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStringForm_t form, uint8_t integer_bits,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value);

/**
 * @brief Converts a value to a Buffer: an Integer gives its 4 or 8 little-endian bytes, a String its
 *        characters and their NUL; a Buffer gives itself.
 *
 * @param heap         The heap.
 * @param space        The namespace whose arena gives the pages.
 * @param value        The value.
 * @param integer_bits 32 or 64.
 * @param out_value    Receives the Buffer, which holds a reference of its own.
 * @return TYPE_MISMATCH for a value that is no Integer, String or Buffer; what allocating returns;
 *         OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_value_to_buffer(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value, uint8_t integer_bits,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value);

/**
 * @brief Copies a value as a Store copies its source: a String or a Buffer into an object of its
 *        own, a Package with every String, Buffer and Package it nests up to 32 deep, the deeper ones
 *        shared; any other value as it is, its reference retained.
 *
 * @param heap      The heap.
 * @param space     The namespace whose arena gives the pages.
 * @param value     The value.
 * @param out_value Receives the copy, which holds references of its own.
 * @return What allocating returns, nothing copied then; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_value_copy(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value);

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
 * @brief The node type a value gives the Name that holds it.
 *
 * @param kind An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_* kind.
 * @return The node type of an Integer, a String, a Buffer or a Package; Uninitialized for any other.
 */
extern uint8_t advanced_configuration_and_power_interface_machine_language_node_type_of_value(uint8_t kind);

/**
 * @brief Reads a buffer field (§5.2): as an Integer when it is no wider than @p integer_bits and was not
 *        made by CreateField, as a Buffer of its bytes otherwise; a field of length 0 reads as an empty
 *        Buffer.
 *
 * @param heap         The heap.
 * @param space        The namespace whose arena gives the pages.
 * @param field        The buffer field.
 * @param integer_bits 32 or 64.
 * @param out_value    Receives the value, which holds a reference of its own.
 * @return INDEX_OUT_OF_RANGE when the field passes its Buffer's end; what allocating returns; OK
 *         otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_buffer_field_read(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *field, uint8_t integer_bits,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value);

/**
 * @brief Writes a value into a buffer field's bits, zero-extended, the Buffer's other bits kept: an
 *        Integer whole, a String or a Buffer through its last field-sized chunk, as Windows writes it
 *        chunk after chunk; a field of length 0 takes nothing.
 *
 * @param heap         The heap.
 * @param field        The buffer field.
 * @param source       An Integer, a String or a Buffer.
 * @param integer_bits 32 or 64.
 * @return INDEX_OUT_OF_RANGE when the field passes its Buffer's end; TYPE_MISMATCH for any other
 *         source; OK otherwise.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_buffer_field_write(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *field,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *source, uint8_t integer_bits);

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
