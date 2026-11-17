#include <kernel/ai/model_slot.h>

#include <kernel/arch/capabilities.h>
#include <kernel/boot/boot_module.h>
#include <kernel/cpu/paging.h>
#include <kernel/diag/telemetry.h>
#include <kernel/drivers/serial.h>

#include <stdckdint.h>

/** Smallest image that can carry a header at all: a version 2 header (lpl/infer/ModelImage.hpp). */
#define KERNEL_MODEL_SLOT_MINIMUM_BYTES 64u

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

uint32_t kernel_model_slot_protect(const ModelSlot_t *slot)
{
#if !KERNEL_ARCH_HAS_WRITE_PROTECT_ENFORCEMENT
    (void) slot;
    return 0u;
#else
    if (slot == NULL || slot->state != MODEL_SLOT_LOADED || slot->bytes == NULL)
        return 0u;

    const uint32_t start = (uint32_t) (uintptr_t) slot->bytes;
    uint32_t end = 0u;

    if (ckd_add(&end, start, slot->size))
        return 0u;
    return paging_set_range_read_only(start, end);
#endif
}

void kernel_model_slot_report(const ModelSlot_t *slot, const char *origin, const char *reading,
                              uint32_t protected_pages)
{
    kernel_telemetry_begin_record(&com1, "model");
    kernel_telemetry_write_text("slot", kernel_model_slot_state_text(slot));
    kernel_telemetry_write_text("origin", origin);
    kernel_telemetry_write_text("reading", reading);
    kernel_telemetry_write_unsigned("image_bytes", slot != NULL ? slot->size : 0u);
    kernel_telemetry_write_unsigned("protected_pages", protected_pages);
    kernel_telemetry_end_record();
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
