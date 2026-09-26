#include <kernel/ai/model_slot.h>

#include <kernel/boot/boot_module.h>

/** Smallest image that can carry a header at all: magic, version, six extents, one length. */
#define KERNEL_MODEL_SLOT_MINIMUM_BYTES 36u

bool kernel_model_slot_probe(ModelSlot_t *out)
{
    if (out == NULL)
        return false;

    out->state = MODEL_SLOT_EMPTY;
    out->bytes = NULL;
    out->size = 0u;
    out->magic = 0u;

    const uint8_t *bytes = NULL;
    uint32_t size = 0u;
    if (!boot_module_find(KERNEL_MODEL_SLOT_SUFFIX, &bytes, &size))
        return false;

    out->bytes = bytes;
    out->size = size;

    if (size < KERNEL_MODEL_SLOT_MINIMUM_BYTES)
    {
        out->state = MODEL_SLOT_MALFORMED;
        return false;
    }

    out->magic =
        (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8) | ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);

    if (out->magic != KERNEL_MODEL_SLOT_MAGIC)
    {
        out->state = MODEL_SLOT_MALFORMED;
        return false;
    }

    out->state = MODEL_SLOT_LOADED;
    return true;
}

const char *kernel_model_slot_state_text(const ModelSlot_t *slot)
{
    if (slot == NULL)
        return "empty";
    switch (slot->state)
    {
    case MODEL_SLOT_LOADED: return "loaded";
    case MODEL_SLOT_MALFORMED: return "malformed";
    case MODEL_SLOT_EMPTY:
    default: return "empty";
    }
}
