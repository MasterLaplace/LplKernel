#include <kernel/acpi/machine_language_objects.h>

#include <string.h>

#define MACHINE_LANGUAGE_OBJECT_ALIGNMENT 16u /**< Bytes every object starts on, and is rounded up to. */
#define MACHINE_LANGUAGE_OBJECT_OFFSET_BITS                                                                            \
    12u /**< Bits of a handle that hold an object's offset in its run, in units of the alignment. */
#define MACHINE_LANGUAGE_MAX_NESTED_PACKAGES 32u /**< Packages one data object nests (§6.1). */
#define MACHINE_LANGUAGE_MAX_PAYLOAD_BYTES                                                                             \
    (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES +                                    \
     1u) /**< The largest payload: a String of the largest length and its NUL; a VarPackage of the most elements       \
            takes as much. */
#define MACHINE_LANGUAGE_OBJECT_FREE 0xFFu /**< The kind of a freed block, while it waits on its size class's list. */
#define MACHINE_LANGUAGE_OBJECT_DYING                                                                                  \
    0x80u /**< Header flag: the object has no reference left and waits to be freed, its count linking the next. */
#define MACHINE_LANGUAGE_SATURATED 0xFFFFFFFFu /**< A reference count that never moves again (§3.5). */
#define MACHINE_LANGUAGE_FINE_CLASS_BYTES                                                                              \
    256u /**< Small blocks up to this size round up to the alignment; larger ones to a quarter of a power of two. */
#define MACHINE_LANGUAGE_CLASSES_PER_DOUBLING 4u /**< Size classes between two powers of two past 256 bytes. */
#define MACHINE_LANGUAGE_RUN_BYTES                                                                                     \
    (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_RUN_PAGES *                                    \
     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES) /**< Bytes of a run of small objects. */

_Static_assert(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LARGE_OBJECT_BYTES ==
                       MACHINE_LANGUAGE_FINE_CLASS_BYTES << 4u &&
                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_SIZE_CLASSES ==
                       MACHINE_LANGUAGE_FINE_CLASS_BYTES / MACHINE_LANGUAGE_OBJECT_ALIGNMENT +
                           4u * MACHINE_LANGUAGE_CLASSES_PER_DOUBLING,
               "the size classes are the fine ones, then four doublings from 256 bytes to the large objects");

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t MachineLanguageStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t MachineLanguageValue_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t MachineLanguageSource_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t MachineLanguageHeader_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t MachineLanguageHeap_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t MachineLanguageNamespace_t;

/**
 * @brief A package being filled: its object, the next element, how many it holds, and its end.
 */
typedef struct MachineLanguagePackageFrame {
    uint32_t handle; /**< The package object. */
    uint32_t index;  /**< The next element read. */
    uint32_t count;  /**< Elements it holds. */
    uint32_t end;    /**< Its PkgLength end. */
} MachineLanguagePackageFrame_t;

/**
 * @brief A Package being copied: the source, its copy, and the next element to copy.
 */
typedef struct MachineLanguageCopyFrame {
    uint32_t source; /**< The source Package's object. */
    uint32_t copy;   /**< Its copy's object. */
    uint32_t index;  /**< The next element copied. */
} MachineLanguageCopyFrame_t;

/**
 * @brief @p value masked to the source's integer width.
 */
static uint64_t machine_language_mask(const MachineLanguageSource_t *source, uint64_t value)
{
    return (source->integer_bits == 32u) ? (value & 0xFFFFFFFFull) : value;
}

/**
 * @brief Reads the integer constant of @p opcode, whose fixed bytes start at @p after; false when the
 *        opcode is no integer constant.
 */
static MachineLanguageStatus_t machine_language_integer_constant(const MachineLanguageSource_t *source, uint32_t opcode,
                                                                 uint32_t after, uint32_t limit, uint64_t *out_value,
                                                                 uint32_t *out_after, bool *out_constant)
{
    static const uint8_t widths[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT] = {
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_BYTE_PREFIX] = 1u,
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_WORD_PREFIX] = 2u,
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_DOUBLE_WORD_PREFIX] = 4u,
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_QUAD_WORD_PREFIX] = 8u,
    };
    uint64_t value = 0u;

    *out_constant = true;
    if (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ZERO ||
        opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ONE ||
        opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ONES)
    {
        value = (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ONES) ? UINT64_MAX :
                (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ONE)  ? 1u :
                                                                                                      0u;
        *out_value = machine_language_mask(source, value);
        *out_after = after;
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    }
    if (widths[opcode] == 0u)
    {
        *out_constant = false;
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    }
    if (limit - after < widths[opcode])
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    for (uint32_t byte = 0u; byte < widths[opcode]; ++byte)
        value |= (uint64_t) source->bytes[after + byte] << (8u * byte);
    *out_value = machine_language_mask(source, value);
    *out_after = after + widths[opcode];
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief An integer masked to @p integer_bits.
 */
static uint64_t machine_language_width_mask(uint8_t integer_bits, uint64_t value)
{
    return (integer_bits == 32u) ? (value & 0xFFFFFFFFull) : value;
}

/**
 * @brief The bytes an object of @p kind and @p length holds after its header.
 */
static uint64_t machine_language_payload_bytes(uint8_t kind, uint32_t length)
{
    switch (kind)
    {
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING: return (uint64_t) length + 1u;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE:
        return (uint64_t) length * sizeof(MachineLanguageValue_t);
    default: return length;
    }
}

/**
 * @brief The bytes of the block an object of @p payload bytes takes: its header and payload, rounded
 *        up to the alignment.
 */
static uint64_t machine_language_block_bytes(uint64_t payload)
{
    return ((uint64_t) sizeof(MachineLanguageHeader_t) + payload + MACHINE_LANGUAGE_OBJECT_ALIGNMENT - 1u) &
           ~(uint64_t) (MACHINE_LANGUAGE_OBJECT_ALIGNMENT - 1u);
}

/**
 * @brief The size class of a small object of @p bytes, a multiple of the alignment below
 *        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LARGE_OBJECT_BYTES: one per multiple
 *        of the alignment up to MACHINE_LANGUAGE_FINE_CLASS_BYTES, then four per power of two, so that an
 *        object that grows keeps meeting blocks its smaller self gave back.
 */
static uint32_t machine_language_size_class(uint32_t bytes)
{
    uint32_t low = MACHINE_LANGUAGE_FINE_CLASS_BYTES;
    uint32_t size_class = MACHINE_LANGUAGE_FINE_CLASS_BYTES / MACHINE_LANGUAGE_OBJECT_ALIGNMENT;

    if (bytes <= MACHINE_LANGUAGE_FINE_CLASS_BYTES)
        return bytes / MACHINE_LANGUAGE_OBJECT_ALIGNMENT - 1u;
    while (bytes > 2u * low)
    {
        low *= 2u;
        size_class += MACHINE_LANGUAGE_CLASSES_PER_DOUBLING;
    }
    return size_class +
           (bytes - low + low / MACHINE_LANGUAGE_CLASSES_PER_DOUBLING - 1u) /
               (low / MACHINE_LANGUAGE_CLASSES_PER_DOUBLING) -
           1u;
}

/**
 * @brief The bytes of a block of size class @p size_class.
 */
static uint32_t machine_language_class_bytes(uint32_t size_class)
{
    const uint32_t fine = MACHINE_LANGUAGE_FINE_CLASS_BYTES / MACHINE_LANGUAGE_OBJECT_ALIGNMENT;

    if (size_class < fine)
        return (size_class + 1u) * MACHINE_LANGUAGE_OBJECT_ALIGNMENT;

    const uint32_t low = MACHINE_LANGUAGE_FINE_CLASS_BYTES
                         << ((size_class - fine) / MACHINE_LANGUAGE_CLASSES_PER_DOUBLING);

    return low + ((size_class - fine) % MACHINE_LANGUAGE_CLASSES_PER_DOUBLING + 1u) *
                     (low / MACHINE_LANGUAGE_CLASSES_PER_DOUBLING);
}

/**
 * @brief Takes a run of @p pages pages from the arena, in the first slot a large object gave back or
 *        past the last; its index, counted from 0, goes to @p out_run.
 */
static MachineLanguageStatus_t machine_language_heap_take_run(MachineLanguageHeap_t *heap,
                                                              MachineLanguageNamespace_t *space, uint32_t pages,
                                                              uint32_t *out_run)
{
    uint32_t run = 0u;

    while (run < heap->run_count && heap->runs[run] != NULL)
        ++run;
    if (run == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OBJECT_RUNS)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_OBJECT_RUNS;

    void *pages_taken = advanced_configuration_and_power_interface_machine_language_namespace_take_pages(space, pages);

    if (pages_taken == NULL)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY;
    heap->runs[run] = pages_taken;
    heap->run_pages[run] = (uint16_t) pages;
    heap->run_count += (run == heap->run_count) ? 1u : 0u;
    *out_run = run;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief Finds a block of @p size_class: the one its class freed last, or a new one cut from the
 *        current run, or from a new run when it does not fit.
 */
static MachineLanguageStatus_t machine_language_heap_take_small_block(MachineLanguageHeap_t *heap,
                                                                      MachineLanguageNamespace_t *space,
                                                                      uint32_t size_class, uint32_t *out_handle)
{
    const uint32_t bytes = machine_language_class_bytes(size_class);
    uint32_t run = 0u;

    if (heap->free_blocks[size_class] != 0u)
    {
        *out_handle = heap->free_blocks[size_class];
        heap->free_blocks[size_class] =
            advanced_configuration_and_power_interface_machine_language_object(heap, *out_handle)->references;
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    }
    if (heap->small_run == 0u || bytes > MACHINE_LANGUAGE_RUN_BYTES - heap->small_used)
    {
        const MachineLanguageStatus_t status = machine_language_heap_take_run(
            heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_RUN_PAGES, &run);

        if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
            return status;
        heap->small_run = run + 1u;
        heap->small_used = 0u;
    }
    *out_handle = (heap->small_run << MACHINE_LANGUAGE_OBJECT_OFFSET_BITS) |
                  (heap->small_used / MACHINE_LANGUAGE_OBJECT_ALIGNMENT);
    heap->small_used += bytes;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief Gives a freed object's block back: a large object's pages to the arena, a small block to the
 *        list of its size class.
 */
static void machine_language_heap_free_block(MachineLanguageHeap_t *heap, MachineLanguageNamespace_t *space,
                                             uint32_t handle, MachineLanguageHeader_t *header)
{
    const uint32_t bytes =
        (uint32_t) machine_language_block_bytes(machine_language_payload_bytes(header->kind, header->length));
    const uint32_t run = (handle >> MACHINE_LANGUAGE_OBJECT_OFFSET_BITS) - 1u;
    const bool large = bytes >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LARGE_OBJECT_BYTES;
    const uint32_t size_class = large ? 0u : machine_language_size_class(bytes);

    heap->object_bytes -= large ? bytes : machine_language_class_bytes(size_class);
    ++heap->objects_freed;
    if (large)
    {
        advanced_configuration_and_power_interface_machine_language_namespace_give_pages(space, heap->runs[run],
                                                                                         heap->run_pages[run]);
        heap->runs[run] = NULL;
        heap->run_pages[run] = 0u;
        return;
    }
    header->kind = MACHINE_LANGUAGE_OBJECT_FREE;
    header->flags = 0u;
    header->references = heap->free_blocks[size_class];
    heap->free_blocks[size_class] = handle;
}

/**
 * @brief Takes one reference off an object; one left with none joins @p chain, linked through its
 *        count, to be freed. A saturated count never moves; a release of an object already freed, or
 *        already on the chain, changes nothing and is counted, since each is a defect.
 */
static void machine_language_count_down(MachineLanguageHeap_t *heap, uint32_t handle, uint32_t *chain)
{
    MachineLanguageHeader_t *header = advanced_configuration_and_power_interface_machine_language_object(heap, handle);

    if (header != NULL && header->references == MACHINE_LANGUAGE_SATURATED)
        return;
    if (header == NULL || header->kind == MACHINE_LANGUAGE_OBJECT_FREE || header->references == 0u ||
        (header->flags & MACHINE_LANGUAGE_OBJECT_DYING) != 0u)
    {
        ++heap->stale_handles;
        return;
    }
    if (--header->references != 0u)
        return;
    header->flags |= MACHINE_LANGUAGE_OBJECT_DYING;
    header->references = *chain;
    *chain = handle;
}

/**
 * @brief The values that follow a package's header.
 */
static MachineLanguageValue_t *
machine_language_elements(AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *header)
{
    return (MachineLanguageValue_t *) (header + 1);
}

/**
 * @brief Builds a String from the characters at @p offset up to their NUL.
 */
static MachineLanguageStatus_t
machine_language_string(AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
                        AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                        const MachineLanguageSource_t *source, uint32_t offset, uint32_t limit,
                        MachineLanguageValue_t *out_value, uint32_t *out_after)
{
    uint32_t end = offset;
    uint32_t handle = 0u;

    while (end < limit && source->bytes[end] != 0x00u)
        ++end;
    if (end == limit)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    if (end - offset > ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BUFFER_BYTES;

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, end - offset, &handle);

    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;
    memcpy(advanced_configuration_and_power_interface_machine_language_object(heap, handle) + 1, source->bytes + offset,
           end - offset);
    out_value->kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING;
    out_value->handle = handle;
    *out_after = end + 1u;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief Builds a Buffer from its size, which must be an integer constant, and its initialiser,
 *        which may make it longer.
 */
static MachineLanguageStatus_t
machine_language_buffer(AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
                        AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                        const MachineLanguageSource_t *source, uint32_t offset, uint32_t end,
                        MachineLanguageValue_t *out_value, bool *out_constant)
{
    uint64_t size = 0u;
    uint32_t initialiser = 0u;
    uint32_t handle = 0u;
    MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_evaluate_integer(
        space, source, offset, end, &size, &initialiser, out_constant);

    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK || !*out_constant)
        return status;

    const uint64_t length = (size > (uint64_t) (end - initialiser)) ? size : (uint64_t) (end - initialiser);

    if (length > ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BUFFER_BYTES;
    status = advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, (uint32_t) length,
        &handle);
    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;
    memcpy(advanced_configuration_and_power_interface_machine_language_object(heap, handle) + 1,
           source->bytes + initialiser, end - initialiser);
    out_value->kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER;
    out_value->handle = handle;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief Opens a Package or a VarPackage: its element count, which a VarPackage gives as an integer
 *        constant, and its object, whose elements start Uninitialized.
 */
static MachineLanguageStatus_t
machine_language_open_package(AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
                              AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                              const MachineLanguageSource_t *source, uint32_t opcode, uint32_t offset, uint32_t end,
                              MachineLanguagePackageFrame_t *out_frame, uint32_t *out_first, bool *out_constant)
{
    uint64_t count = 0u;
    uint32_t first = offset + 1u;
    MachineLanguageStatus_t status = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    uint32_t cap = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_PACKAGE_ELEMENTS;

    *out_constant = true;
    if (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_PACKAGE)
    {
        if (offset >= end)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
        count = source->bytes[offset];
    }
    else
    {
        cap = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VARIABLE_PACKAGE_ELEMENTS;
        status = advanced_configuration_and_power_interface_machine_language_evaluate_integer(
            space, source, offset, end, &count, &first, out_constant);
        if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK || !*out_constant)
            return status;
    }
    if (count > cap)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PACKAGE_ELEMENTS;
    status = advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE, (uint32_t) count,
        &out_frame->handle);
    out_frame->index = 0u;
    out_frame->count = (uint32_t) count;
    out_frame->end = end;
    *out_first = first;
    return status;
}

/**
 * @brief Gives back the packages a data object left open, each with what it holds, and returns
 *        @p status: a data object that waits for the interpreter, or is malformed, keeps nothing.
 */
static MachineLanguageStatus_t machine_language_abandon(MachineLanguageHeap_t *heap, MachineLanguageNamespace_t *space,
                                                        const MachineLanguagePackageFrame_t *frames, uint32_t depth,
                                                        MachineLanguageStatus_t status)
{
    while (depth != 0u)
        advanced_configuration_and_power_interface_machine_language_object_release(heap, space, frames[--depth].handle);
    return status;
}

/**
 * @brief The Buffer a buffer field is made over, when the field's bits lie inside it; NULL otherwise.
 */
static MachineLanguageHeader_t *machine_language_field_buffer(const MachineLanguageHeap_t *heap,
                                                              const MachineLanguageValue_t *field)
{
    MachineLanguageHeader_t *buffer =
        advanced_configuration_and_power_interface_machine_language_object(heap, field->handle);
    const uint64_t end = (field->payload & 0xFFFFFFFFull) + (field->payload >> 32u);

    if (buffer == NULL || buffer->kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER ||
        end > (uint64_t) buffer->length * 8u)
        return NULL;
    return buffer;
}

/**
 * @brief Bit @p index of @p bytes, 0 or 1.
 */
static uint32_t machine_language_bit(const uint8_t *bytes, uint64_t index)
{
    return (bytes[index / 8u] >> (index % 8u)) & 1u;
}

/**
 * @brief The value of a hexadecimal digit, either case, or 16 for any other character.
 */
static uint32_t machine_language_digit_value(uint8_t character)
{
    if (character >= '0' && character <= '9')
        return (uint32_t) (character - '0');
    if (character >= 'A' && character <= 'F')
        return (uint32_t) (character - 'A') + 10u;
    if (character >= 'a' && character <= 'f')
        return (uint32_t) (character - 'a') + 10u;
    return 16u;
}

/**
 * @brief Whether ToInteger skips a character before a number: a space, or a tab up to a carriage
 *        return.
 */
static bool machine_language_is_white_space(uint8_t character)
{
    return character == ' ' || (character >= '\t' && character <= '\r');
}

/**
 * @brief Whether @p characters start with 0x or 0X.
 */
static bool machine_language_has_hexadecimal_prefix(const uint8_t *characters, uint32_t length)
{
    return length >= 2u && characters[0] == '0' && (characters[1] == 'x' || characters[1] == 'X');
}

/**
 * @brief Reads characters as an implicit conversion does: hexadecimal past an optional 0x prefix, up to
 *        the first non-hexadecimal character or @p maximum_digits digits.
 */
static uint64_t machine_language_read_hexadecimal(const uint8_t *characters, uint32_t length, uint32_t maximum_digits)
{
    uint32_t index = machine_language_has_hexadecimal_prefix(characters, length) ? 2u : 0u;
    uint64_t value = 0u;

    for (uint32_t digits = 0u; index < length && digits < maximum_digits; ++index, ++digits)
    {
        const uint32_t digit = machine_language_digit_value(characters[index]);

        if (digit == 16u)
            break;
        value = (value << 4u) | digit;
    }
    return value;
}

/**
 * @brief The base ToInteger reads a number in, past its prefix: 16 after 0x, 8 after a leading 0,
 *        10 otherwise; @p index moves past the prefix.
 */
static uint32_t machine_language_explicit_base(const uint8_t *characters, uint32_t length, uint32_t *index)
{
    if (*index >= length || characters[*index] != '0')
        return 10u;
    if (machine_language_has_hexadecimal_prefix(characters + *index, length - *index))
    {
        *index += 2u;
        return 16u;
    }
    ++*index;
    return 8u;
}

/**
 * @brief Reads characters as ToInteger does: past white space and a sign, in the base its prefix
 *        gives, up to the first character that is no digit of that base, saturated at Ones.
 */
static uint64_t machine_language_read_explicitly(const uint8_t *characters, uint32_t length)
{
    uint32_t index = 0u;
    bool negative = false;
    uint64_t value = 0u;

    while (index < length && machine_language_is_white_space(characters[index]))
        ++index;
    if (index < length && (characters[index] == '-' || characters[index] == '+'))
        negative = characters[index++] == '-';

    const uint32_t base = machine_language_explicit_base(characters, length, &index);

    for (; index < length; ++index)
    {
        const uint32_t digit = machine_language_digit_value(characters[index]);

        if (digit >= base)
            break;
        if (value > (UINT64_MAX - digit) / base)
        {
            value = UINT64_MAX;
            break;
        }
        value = value * base + digit;
    }
    return negative ? 0u - value : value;
}

/**
 * @brief Writes @p value in @p base, uppercase, with at least @p minimum_digits digits; gives how many
 *        it wrote, at most 20.
 */
static uint32_t machine_language_write_digits(uint64_t value, uint32_t base, uint32_t minimum_digits, uint8_t *out)
{
    static const char digits[] = "0123456789ABCDEF";
    uint8_t reversed[20];
    uint32_t count = 0u;

    do
    {
        reversed[count++] = (uint8_t) digits[value % base];
        value /= base;
    } while (value != 0u || count < minimum_digits);
    for (uint32_t index = 0u; index < count; ++index)
        out[index] = reversed[count - 1u - index];
    return count;
}

/**
 * @brief The characters a Buffer of @p length bytes takes as a String in @p form.
 */
static uint64_t
machine_language_buffer_string_length(const uint8_t *bytes, uint32_t length,
                                      AdvancedConfigurationAndPowerInterfaceMachineLanguageStringForm_t form)
{
    uint64_t characters = (length != 0u) ? length - 1u : 0u;

    for (uint32_t index = 0u; index < length; ++index)
        characters += (form != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_DECIMAL) ? 4u :
                      (bytes[index] >= 100u)                                                               ? 3u :
                      (bytes[index] >= 10u)                                                                ? 2u :
                                                                                                             1u;
    return characters;
}

/**
 * @brief Writes a Buffer's bytes as a String in @p form at @p out, which holds as many characters as
 *        machine_language_buffer_string_length gives.
 */
static void machine_language_write_buffer_string(const uint8_t *bytes, uint32_t length,
                                                 AdvancedConfigurationAndPowerInterfaceMachineLanguageStringForm_t form,
                                                 uint8_t *out)
{
    const bool implicit = form == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_IMPLICIT;
    const bool decimal = form == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_DECIMAL;

    for (uint32_t index = 0u; index < length; ++index)
    {
        if (index != 0u)
            *out++ = implicit ? ' ' : ',';
        if (!decimal)
        {
            *out++ = '0';
            *out++ = 'x';
        }
        out += machine_language_write_digits(bytes[index], decimal ? 10u : 16u, decimal ? 1u : 2u, out);
    }
}

/**
 * @brief Builds a String of @p length characters copied from @p characters.
 */
static MachineLanguageStatus_t machine_language_string_of(MachineLanguageHeap_t *heap,
                                                          MachineLanguageNamespace_t *space, const uint8_t *characters,
                                                          uint32_t length, MachineLanguageValue_t *out_value)
{
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, length, out_value);

    if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        memcpy(advanced_configuration_and_power_interface_machine_language_object_payload(heap, out_value->handle),
               characters, length);
    return status;
}

/**
 * @brief Writes an Integer as a String in @p form.
 */
static MachineLanguageStatus_t
machine_language_integer_string(MachineLanguageHeap_t *heap, MachineLanguageNamespace_t *space, uint64_t integer,
                                AdvancedConfigurationAndPowerInterfaceMachineLanguageStringForm_t form,
                                uint8_t integer_bits, MachineLanguageValue_t *out_value)
{
    uint8_t characters[24];
    uint32_t length = 0u;

    if (form == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_DECIMAL)
        length = machine_language_write_digits(integer, 10u, 1u, characters);
    else if (form == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_HEXADECIMAL)
    {
        characters[0] = '0';
        characters[1] = 'x';
        length = 2u + machine_language_write_digits(integer, 16u, 1u, characters + 2);
    }
    else
        length = machine_language_write_digits(integer, 16u, integer_bits / 4u, characters);
    return machine_language_string_of(heap, space, characters, length, out_value);
}

/**
 * @brief Copies a String or a Buffer into an object of its own.
 */
static MachineLanguageStatus_t machine_language_copy_bytes(MachineLanguageHeap_t *heap,
                                                           MachineLanguageNamespace_t *space,
                                                           const MachineLanguageValue_t *value,
                                                           MachineLanguageValue_t *out_value)
{
    const MachineLanguageHeader_t *header =
        advanced_configuration_and_power_interface_machine_language_object(heap, value->handle);
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_allocate(
        heap, space, value->kind, header->length, out_value);

    if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        memcpy(advanced_configuration_and_power_interface_machine_language_object_payload(heap, out_value->handle),
               header + 1, header->length);
    return status;
}

/**
 * @brief Copies one element of a Package being copied into @p out_element: a String or a Buffer into
 *        an object of its own, a Package into an empty one its elements will fill, anything else as it
 *        is, its reference retained.
 */
static MachineLanguageStatus_t machine_language_copy_element(MachineLanguageHeap_t *heap,
                                                             MachineLanguageNamespace_t *space,
                                                             const MachineLanguageValue_t *element, bool nests,
                                                             MachineLanguageValue_t *out_element)
{
    if (element->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING ||
        element->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER)
        return machine_language_copy_bytes(heap, space, element, out_element);
    if (element->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE && nests)
        return advanced_configuration_and_power_interface_machine_language_value_allocate(
            heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE,
            advanced_configuration_and_power_interface_machine_language_object(heap, element->handle)->length,
            out_element);
    *out_element = *element;
    advanced_configuration_and_power_interface_machine_language_value_retain(heap, element);
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief Copies a Package and what it nests, through an explicit stack of
 *        MACHINE_LANGUAGE_MAX_NESTED_PACKAGES levels; deeper packages are shared.
 */
static MachineLanguageStatus_t machine_language_copy_package(MachineLanguageHeap_t *heap,
                                                             MachineLanguageNamespace_t *space,
                                                             const MachineLanguageValue_t *value,
                                                             MachineLanguageValue_t *out_value)
{
    MachineLanguageCopyFrame_t frames[MACHINE_LANGUAGE_MAX_NESTED_PACKAGES];
    uint32_t depth = 1u;
    MachineLanguageStatus_t status = machine_language_copy_element(heap, space, value, true, out_value);

    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;
    frames[0].source = value->handle;
    frames[0].copy = out_value->handle;
    frames[0].index = 0u;
    while (depth != 0u)
    {
        MachineLanguageCopyFrame_t *frame = &frames[depth - 1u];
        const MachineLanguageHeader_t *source =
            advanced_configuration_and_power_interface_machine_language_object(heap, frame->source);

        if (frame->index == source->length)
        {
            --depth;
            continue;
        }

        const MachineLanguageValue_t *element = (const MachineLanguageValue_t *) (source + 1) + frame->index;
        MachineLanguageValue_t *copy =
            (MachineLanguageValue_t *) advanced_configuration_and_power_interface_machine_language_object_payload(
                heap, frame->copy) +
            frame->index;
        const bool nests = depth < MACHINE_LANGUAGE_MAX_NESTED_PACKAGES;

        ++frame->index;
        status = machine_language_copy_element(heap, space, element, nests, copy);
        if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        {
            advanced_configuration_and_power_interface_machine_language_value_release(heap, space, out_value);
            return status;
        }
        if (element->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE && nests)
        {
            frames[depth].source = element->handle;
            frames[depth].copy = copy->handle;
            frames[depth].index = 0u;
            ++depth;
        }
    }
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_object_allocate(
    MachineLanguageHeap_t *heap, MachineLanguageNamespace_t *space, uint8_t kind, uint32_t length, uint32_t *out_handle)
{
    const uint64_t payload = machine_language_payload_bytes(kind, length);
    uint32_t handle = 0u;
    uint32_t run = 0u;
    MachineLanguageStatus_t status = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

    if (payload > MACHINE_LANGUAGE_MAX_PAYLOAD_BYTES)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BUFFER_BYTES;

    const uint32_t bytes = (uint32_t) machine_language_block_bytes(payload);
    uint32_t block_bytes = bytes;

    if (bytes >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LARGE_OBJECT_BYTES)
    {
        status = machine_language_heap_take_run(
            heap, space,
            (bytes + ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES - 1u) /
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES,
            &run);
        handle = (run + 1u) << MACHINE_LANGUAGE_OBJECT_OFFSET_BITS;
    }
    else
    {
        block_bytes = machine_language_class_bytes(machine_language_size_class(bytes));
        status = machine_language_heap_take_small_block(heap, space, machine_language_size_class(bytes), &handle);
    }
    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;

    MachineLanguageHeader_t *header = advanced_configuration_and_power_interface_machine_language_object(heap, handle);

    memset(header, 0, block_bytes);
    header->references = 1u;
    header->kind = kind;
    header->length = length;
    ++heap->objects;
    heap->object_bytes += block_bytes;
    if (heap->object_bytes > heap->object_bytes_peak)
        heap->object_bytes_peak = heap->object_bytes;
    *out_handle = handle;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

MachineLanguageHeader_t *
advanced_configuration_and_power_interface_machine_language_object(const MachineLanguageHeap_t *heap, uint32_t handle)
{
    const uint32_t run = handle >> MACHINE_LANGUAGE_OBJECT_OFFSET_BITS;

    if (run == 0u || run > heap->run_count || heap->runs[run - 1u] == NULL)
        return NULL;
    return (MachineLanguageHeader_t *) ((uint8_t *) heap->runs[run - 1u] +
                                        (handle & ((1u << MACHINE_LANGUAGE_OBJECT_OFFSET_BITS) - 1u)) *
                                            MACHINE_LANGUAGE_OBJECT_ALIGNMENT);
}

void advanced_configuration_and_power_interface_machine_language_object_retain(MachineLanguageHeap_t *heap,
                                                                               uint32_t handle)
{
    MachineLanguageHeader_t *header = advanced_configuration_and_power_interface_machine_language_object(heap, handle);

    if (header != NULL && header->references == MACHINE_LANGUAGE_SATURATED)
        return;
    if (header == NULL || header->kind == MACHINE_LANGUAGE_OBJECT_FREE || header->references == 0u ||
        (header->flags & MACHINE_LANGUAGE_OBJECT_DYING) != 0u)
    {
        ++heap->stale_handles;
        return;
    }
    ++header->references;
}

void advanced_configuration_and_power_interface_machine_language_object_release(MachineLanguageHeap_t *heap,
                                                                                MachineLanguageNamespace_t *space,
                                                                                uint32_t handle)
{
    uint32_t chain = 0u;

    machine_language_count_down(heap, handle, &chain);
    while (chain != 0u)
    {
        const uint32_t freed = chain;
        MachineLanguageHeader_t *header =
            advanced_configuration_and_power_interface_machine_language_object(heap, freed);

        chain = header->references;
        if (header->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE)
        {
            const MachineLanguageValue_t *elements = (const MachineLanguageValue_t *) (header + 1);

            for (uint32_t index = 0u; index < header->length; ++index)
                if (advanced_configuration_and_power_interface_machine_language_value_holds_object(
                        elements[index].kind))
                    machine_language_count_down(heap, elements[index].handle, &chain);
        }
        machine_language_heap_free_block(heap, space, freed, header);
    }
}

bool advanced_configuration_and_power_interface_machine_language_value_holds_object(uint8_t kind)
{
    switch (kind)
    {
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING:
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER:
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE:
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION:
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_FIELD_UNIT:
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD:
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_ELEMENT:
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_BYTE: return true;
    default: return false;
    }
}

void advanced_configuration_and_power_interface_machine_language_value_retain(MachineLanguageHeap_t *heap,
                                                                              const MachineLanguageValue_t *value)
{
    if (advanced_configuration_and_power_interface_machine_language_value_holds_object(value->kind))
        advanced_configuration_and_power_interface_machine_language_object_retain(heap, value->handle);
}

void advanced_configuration_and_power_interface_machine_language_value_release(MachineLanguageHeap_t *heap,
                                                                               MachineLanguageNamespace_t *space,
                                                                               MachineLanguageValue_t *value)
{
    if (advanced_configuration_and_power_interface_machine_language_value_holds_object(value->kind))
        advanced_configuration_and_power_interface_machine_language_object_release(heap, space, value->handle);
    value->kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_UNINITIALIZED;
    value->flags = 0u;
    value->handle = 0u;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_value_allocate(
    MachineLanguageHeap_t *heap, MachineLanguageNamespace_t *space, uint8_t kind, uint32_t length,
    MachineLanguageValue_t *out_value)
{
    memset(out_value, 0, sizeof(*out_value));

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, kind, length, &out_value->handle);

    if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        out_value->kind = kind;
    return status;
}

uint8_t *advanced_configuration_and_power_interface_machine_language_object_payload(const MachineLanguageHeap_t *heap,
                                                                                    uint32_t handle)
{
    MachineLanguageHeader_t *header = advanced_configuration_and_power_interface_machine_language_object(heap, handle);

    return (header != NULL) ? (uint8_t *) (header + 1) : NULL;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_value_bytes(
    const MachineLanguageHeap_t *heap, const MachineLanguageValue_t *value, uint8_t integer_bits, uint8_t scratch[8],
    const uint8_t **out_bytes, uint32_t *out_length)
{
    if (value->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
    {
        for (uint32_t byte = 0u; byte < 8u; ++byte)
            scratch[byte] = (uint8_t) (value->payload >> (8u * byte));
        *out_bytes = scratch;
        *out_length = integer_bits / 8u;
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    }
    if (value->kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING &&
        value->kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH;

    const MachineLanguageHeader_t *header =
        advanced_configuration_and_power_interface_machine_language_object(heap, value->handle);

    *out_bytes = (const uint8_t *) (header + 1);
    *out_length = header->length;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_value_to_integer(
    const MachineLanguageHeap_t *heap, const MachineLanguageValue_t *value, uint8_t integer_bits, uint64_t *out_integer,
    bool *out_forbidden)
{
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    uint64_t integer = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_bytes(
        heap, value, integer_bits, scratch, &bytes, &length);

    *out_forbidden = false;
    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;
    if (value->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING)
    {
        *out_forbidden = length == 0u || machine_language_has_hexadecimal_prefix(bytes, length);
        integer = machine_language_read_hexadecimal(bytes, length, integer_bits / 4u);
    }
    else
    {
        *out_forbidden = length == 0u;
        for (uint32_t byte = 0u; byte < length && byte < integer_bits / 8u; ++byte)
            integer |= (uint64_t) bytes[byte] << (8u * byte);
    }
    *out_integer = machine_language_width_mask(integer_bits, integer);
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_value_to_integer_explicitly(
    const MachineLanguageHeap_t *heap, const MachineLanguageValue_t *value, uint8_t integer_bits, uint64_t *out_integer)
{
    bool forbidden = false;

    if (value->kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING)
        return advanced_configuration_and_power_interface_machine_language_value_to_integer(heap, value, integer_bits,
                                                                                            out_integer, &forbidden);

    const MachineLanguageHeader_t *header =
        advanced_configuration_and_power_interface_machine_language_object(heap, value->handle);

    *out_integer = machine_language_width_mask(
        integer_bits, machine_language_read_explicitly((const uint8_t *) (header + 1), header->length));
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_value_to_string(
    MachineLanguageHeap_t *heap, MachineLanguageNamespace_t *space, const MachineLanguageValue_t *value,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStringForm_t form, uint8_t integer_bits,
    MachineLanguageValue_t *out_value)
{
    if (value->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING)
    {
        *out_value = *value;
        advanced_configuration_and_power_interface_machine_language_value_retain(heap, value);
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    }
    if (value->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return machine_language_integer_string(heap, space, machine_language_width_mask(integer_bits, value->payload),
                                               form, integer_bits, out_value);
    if (value->kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH;

    const MachineLanguageHeader_t *buffer =
        advanced_configuration_and_power_interface_machine_language_object(heap, value->handle);
    const uint64_t length = machine_language_buffer_string_length((const uint8_t *) (buffer + 1), buffer->length, form);

    if (length > ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BUFFER_BYTES;

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, (uint32_t) length,
        out_value);

    if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        machine_language_write_buffer_string(
            (const uint8_t *) (buffer + 1), buffer->length, form,
            advanced_configuration_and_power_interface_machine_language_object_payload(heap, out_value->handle));
    return status;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_value_to_buffer(
    MachineLanguageHeap_t *heap, MachineLanguageNamespace_t *space, const MachineLanguageValue_t *value,
    uint8_t integer_bits, MachineLanguageValue_t *out_value)
{
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_bytes(
        heap, value, integer_bits, scratch, &bytes, &length);

    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;
    if (value->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER)
    {
        *out_value = *value;
        advanced_configuration_and_power_interface_machine_language_value_retain(heap, value);
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    }

    const uint32_t size =
        length + ((value->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING) ? 1u : 0u);

    status = advanced_configuration_and_power_interface_machine_language_value_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, size, out_value);
    if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        memcpy(advanced_configuration_and_power_interface_machine_language_object_payload(heap, out_value->handle),
               bytes, length);
    return status;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_value_copy(
    MachineLanguageHeap_t *heap, MachineLanguageNamespace_t *space, const MachineLanguageValue_t *value,
    MachineLanguageValue_t *out_value)
{
    if (value->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE)
        return machine_language_copy_package(heap, space, value, out_value);
    return machine_language_copy_element(heap, space, value, false, out_value);
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_buffer_field_read(
    MachineLanguageHeap_t *heap, MachineLanguageNamespace_t *space, const MachineLanguageValue_t *field,
    uint8_t integer_bits, MachineLanguageValue_t *out_value)
{
    const MachineLanguageHeader_t *buffer = machine_language_field_buffer(heap, field);
    const uint32_t offset = (uint32_t) (field->payload & 0xFFFFFFFFull);
    const uint32_t length = (uint32_t) (field->payload >> 32u);

    if (buffer == NULL)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INDEX_OUT_OF_RANGE;
    if ((field->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BUFFER_FIELD_READS_AS_BUFFER) ==
            0u &&
        length != 0u && length <= integer_bits)
    {
        memset(out_value, 0, sizeof(*out_value));
        out_value->kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER;
        for (uint32_t bit = 0u; bit < length; ++bit)
            out_value->payload |= (uint64_t) machine_language_bit((const uint8_t *) (buffer + 1), offset + bit) << bit;
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    }

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, (length + 7u) / 8u,
        out_value);

    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;

    uint8_t *bytes =
        advanced_configuration_and_power_interface_machine_language_object_payload(heap, out_value->handle);

    for (uint32_t bit = 0u; bit < length; ++bit)
        bytes[bit / 8u] |= (uint8_t) (machine_language_bit((const uint8_t *) (buffer + 1), offset + bit) << (bit % 8u));
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_buffer_field_write(
    MachineLanguageHeap_t *heap, const MachineLanguageValue_t *field, const MachineLanguageValue_t *source,
    uint8_t integer_bits)
{
    MachineLanguageHeader_t *buffer = machine_language_field_buffer(heap, field);
    const uint32_t offset = (uint32_t) (field->payload & 0xFFFFFFFFull);
    const uint32_t length = (uint32_t) (field->payload >> 32u);
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t count = 0u;

    if (buffer == NULL)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INDEX_OUT_OF_RANGE;

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_bytes(
        heap, source, integer_bits, scratch, &bytes, &count);

    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK || length == 0u || count == 0u)
        return status;
    if (source->kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
    {
        const uint32_t chunk = (length + 7u) / 8u;
        const uint32_t last = (count - 1u) / chunk * chunk;

        bytes += last;
        count -= last;
    }

    uint8_t *target = (uint8_t *) (buffer + 1);
    const bool backwards =
        bytes >= target && bytes < target + buffer->length && (uint64_t) (bytes - target) * 8u < offset;

    for (uint32_t step = 0u; step < length; ++step)
    {
        const uint32_t bit = backwards ? length - 1u - step : step;
        const uint64_t index = (uint64_t) offset + bit;
        const uint32_t value = (bit / 8u < count) ? machine_language_bit(bytes, bit) : 0u;

        target[index / 8u] = (uint8_t) ((target[index / 8u] & ~(1u << (index % 8u))) | (value << (index % 8u)));
    }
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

void advanced_configuration_and_power_interface_machine_language_heap_close(MachineLanguageHeap_t *heap,
                                                                            MachineLanguageNamespace_t *space)
{
    for (uint32_t run = 0u; run < heap->run_count; ++run)
        if (heap->runs[run] != NULL)
            advanced_configuration_and_power_interface_machine_language_namespace_give_pages(space, heap->runs[run],
                                                                                             heap->run_pages[run]);
    memset(heap, 0, sizeof(*heap));
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_evaluate_integer(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, const MachineLanguageSource_t *source,
    uint32_t offset, uint32_t limit, uint64_t *out_integer, uint32_t *out_after, bool *out_constant)
{
    uint32_t opcode = 0u;
    uint32_t after = 0u;

    *out_constant = false;
    if (offset >= limit)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    if (advanced_configuration_and_power_interface_machine_language_starts_name(source->bytes[offset]))
    {
        AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t name;
        uint32_t index = 0u;
        MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
            source->bytes, offset, limit, &name, &after);

        if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
            return status;
        if (advanced_configuration_and_power_interface_machine_language_namespace_resolve(
                space, source->scope, source->bytes, &name, &index) !=
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);

        if (node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
        *out_integer = node->value.payload;
        *out_after = after;
        *out_constant = true;
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    }

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_opcode(
        source->bytes, offset, limit, &opcode, &after);

    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;
    return machine_language_integer_constant(source, opcode, after, limit, out_integer, out_after, out_constant);
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_evaluate_data(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, const MachineLanguageSource_t *source,
    uint32_t offset, uint32_t limit, MachineLanguageValue_t *out_value, bool *out_constant)
{
    MachineLanguagePackageFrame_t frames[MACHINE_LANGUAGE_MAX_NESTED_PACKAGES];
    uint32_t depth = 0u;
    uint32_t pc = offset;
    MachineLanguageStatus_t status = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

    memset(out_value, 0, sizeof(*out_value));
    *out_constant = false;
    for (;;)
    {
        MachineLanguageValue_t value = {0};
        const uint32_t end = (depth != 0u) ? frames[depth - 1u].end : limit;
        uint32_t opcode = 0u;
        uint32_t after = 0u;
        bool constant = true;

        if (depth != 0u && pc == end)
        {
            --depth;
            heap->packages_padded += (frames[depth].index < frames[depth].count) ? 1u : 0u;
            heap->packages_truncated += (frames[depth].index > frames[depth].count) ? 1u : 0u;
            value.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE;
            value.handle = frames[depth].handle;
        }
        else if (pc >= end)
            return machine_language_abandon(
                heap, space, frames, depth,
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED);
        else if (advanced_configuration_and_power_interface_machine_language_starts_name(source->bytes[pc]))
        {
            AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t name;

            if (depth == 0u)
                return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
            status = advanced_configuration_and_power_interface_machine_language_read_name_string(source->bytes, pc,
                                                                                                  end, &name, &after);
            if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
                return machine_language_abandon(heap, space, frames, depth, status);
            value.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_NAME_REFERENCE;
            value.auxiliary = source->table;
            value.handle = source->scope;
            value.payload = pc;
            pc = after;
        }
        else
        {
            status = advanced_configuration_and_power_interface_machine_language_read_opcode(source->bytes, pc, end,
                                                                                             &opcode, &after);
            if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
                return machine_language_abandon(heap, space, frames, depth, status);
            if (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_STRING_PREFIX)
                status = machine_language_string(heap, space, source, after, end, &value, &pc);
            else if (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_BUFFER ||
                     opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_PACKAGE ||
                     opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_VARIABLE_PACKAGE)
            {
                uint32_t package_end = 0u;
                uint32_t first = 0u;

                status = advanced_configuration_and_power_interface_machine_language_read_package_end(
                    source->bytes, after, end, &package_end, &after);
                if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                    opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_BUFFER)
                {
                    status = machine_language_buffer(heap, space, source, after, package_end, &value, &constant);
                    pc = package_end;
                }
                else if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
                {
                    if (depth == MACHINE_LANGUAGE_MAX_NESTED_PACKAGES)
                        return machine_language_abandon(
                            heap, space, frames, depth,
                            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK);
                    status = machine_language_open_package(heap, space, source, opcode, after, package_end,
                                                           &frames[depth], &first, &constant);
                    if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK && constant)
                    {
                        ++depth;
                        pc = first;
                        continue;
                    }
                }
            }
            else
            {
                status = machine_language_integer_constant(source, opcode, after, end, &value.payload, &pc, &constant);
                value.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER;
            }
            if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK || !constant)
                return machine_language_abandon(heap, space, frames, depth, status);
        }
        if (depth == 0u)
        {
            *out_value = value;
            *out_constant = true;
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
        }

        MachineLanguagePackageFrame_t *frame = &frames[depth - 1u];

        if (frame->index < frame->count)
            machine_language_elements(advanced_configuration_and_power_interface_machine_language_object(
                heap, frame->handle))[frame->index] = value;
        else
            advanced_configuration_and_power_interface_machine_language_value_release(heap, space, &value);
        ++frame->index;
    }
}

uint8_t advanced_configuration_and_power_interface_machine_language_node_type_of_value(uint8_t kind)
{
    switch (kind)
    {
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER:
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_INTEGER;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING:
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_STRING;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER:
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_BUFFER;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE:
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PACKAGE;
    default: return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_UNINITIALIZED;
    }
}
