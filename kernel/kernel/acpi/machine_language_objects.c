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
#define MACHINE_LANGUAGE_RUN_BYTES                                                                                     \
    (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_RUN_PAGES *                                    \
     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES) /**< Bytes of a run of small objects. */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t MachineLanguageStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t MachineLanguageValue_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t MachineLanguageSource_t;

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
 * @brief Takes a run of @p pages pages from the arena; its index, counted from 0, goes to @p out_run.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
machine_language_heap_take_run(AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
                               AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t pages,
                               uint32_t *out_run)
{
    if (heap->run_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OBJECT_RUNS)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_OBJECT_RUNS;

    void *run = advanced_configuration_and_power_interface_machine_language_namespace_take_pages(space, pages);

    if (run == NULL)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY;
    heap->runs[heap->run_count] = run;
    heap->run_pages[heap->run_count] = (uint16_t) pages;
    *out_run = heap->run_count++;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
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
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, end - offset,
        end - offset + 1u, &handle);

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
        (uint32_t) length, &handle);
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
        (uint32_t) count * (uint32_t) sizeof(MachineLanguageValue_t), &out_frame->handle);
    out_frame->index = 0u;
    out_frame->count = (uint32_t) count;
    out_frame->end = end;
    *out_first = first;
    return status;
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_object_allocate(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint8_t kind, uint32_t length,
    uint32_t payload_bytes, uint32_t *out_handle)
{
    const uint32_t bytes = ((uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t) +
                            payload_bytes + MACHINE_LANGUAGE_OBJECT_ALIGNMENT - 1u) &
                           ~(MACHINE_LANGUAGE_OBJECT_ALIGNMENT - 1u);
    uint32_t run = 0u;
    uint32_t offset = 0u;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

    if (payload_bytes > MACHINE_LANGUAGE_MAX_PAYLOAD_BYTES)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BUFFER_BYTES;
    if (bytes >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LARGE_OBJECT_BYTES)
        status = machine_language_heap_take_run(
            heap, space,
            (bytes + ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES - 1u) /
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES,
            &run);
    else
    {
        if (heap->small_run == 0u || bytes > MACHINE_LANGUAGE_RUN_BYTES - heap->small_used)
        {
            status = machine_language_heap_take_run(
                heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_RUN_PAGES, &run);
            if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
                return status;
            heap->small_run = run + 1u;
            heap->small_used = 0u;
        }
        run = heap->small_run - 1u;
        offset = heap->small_used;
        heap->small_used += bytes;
    }
    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;

    AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *header =
        (AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *) ((uint8_t *) heap->runs[run] + offset);

    memset(header, 0, bytes);
    header->kind = kind;
    header->length = length;
    ++heap->objects;
    heap->object_bytes += bytes;
    *out_handle = ((run + 1u) << MACHINE_LANGUAGE_OBJECT_OFFSET_BITS) | (offset / MACHINE_LANGUAGE_OBJECT_ALIGNMENT);
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *
advanced_configuration_and_power_interface_machine_language_object(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap, uint32_t handle)
{
    const uint32_t run = handle >> MACHINE_LANGUAGE_OBJECT_OFFSET_BITS;

    if (run == 0u || run > heap->run_count)
        return NULL;
    return (AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t
                *) ((uint8_t *) heap->runs[run - 1u] +
                    (handle & ((1u << MACHINE_LANGUAGE_OBJECT_OFFSET_BITS) - 1u)) * MACHINE_LANGUAGE_OBJECT_ALIGNMENT);
}

void advanced_configuration_and_power_interface_machine_language_heap_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space)
{
    for (uint32_t run = 0u; run < heap->run_count; ++run)
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
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
        else if (advanced_configuration_and_power_interface_machine_language_starts_name(source->bytes[pc]))
        {
            AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t name;

            if (depth == 0u)
                return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
            status = advanced_configuration_and_power_interface_machine_language_read_name_string(source->bytes, pc,
                                                                                                  end, &name, &after);
            if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
                return status;
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
                return status;
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
                        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
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
                return status;
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
