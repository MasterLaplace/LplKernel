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
 * @file model_slot.h
 * @brief The weights blob, delivered like a cartridge.
 *
 * GRUB hands the kernel a module; the physical memory manager must exclude its
 * pages from the free lists or something will allocate over the demon's mind. That
 * reservation already exists for the cartridge and is reused here rather than
 * reinvented — and tested by overlap, not inclusion, because a module almost never
 * starts on a page boundary.
 *
 * So there is deliberately no reservation code in this file. `pmm_is_boot_module_page`
 * withholds every page any module covers, whatever its name; a second mechanism that
 * knew about `.lplmind` specifically would be a second thing to keep correct, and the
 * day the two disagreed the symptom would be a model that decodes to noise.
 *
 * What IS here is the honest report. An absent module is a legitimate configuration
 * — a debug image's tests derive their weights from a seed — and an invalid one is a fault.
 * The two are distinguished and neither is silently replaced by the other.
 *
 * Why the slot takes a BAKED image and not a `.gguf` directly, since that was the
 * obvious first idea: the two questions a container answers are not the same one.
 * GGUF opens with a key/value map of arbitrary strings and an arbitrary tensor
 * layout, so reading it means a text parser and a dispatch over quantisation formats
 * — in ring 0, on an untrusted file. `.lplmind` is that file already resolved, the
 * same reader/writer split the project draws between `.lplscene` and `.lplpak`: the
 * host converts once, the kernel maps what comes out. A GGUF-to-`.lplmind`
 * conversion therefore belongs on the writer side, next to `lpl-bake`.
 *
 * And the practical reason the parity gate must NOT depend on this slot: a module
 * is read off the emulated CD-ROM by the bootloader, one sector at a time, before
 * the kernel gets control. A few hundred megabytes of weights is minutes of QEMU
 * start-up on every single run. The gate derives its weights from a seed and stays
 * a gate about arithmetic; the slot is how a real mind arrives when one is wanted.
 *
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-08-05
 **************************************************************************/

#ifndef KERNEL_AI_MODEL_SLOT_H
#define KERNEL_AI_MODEL_SLOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** File name suffix a weights module must carry. */
#define KERNEL_MODEL_SLOT_SUFFIX ".lplmind"

/** First four bytes of a weights image, little-endian "LPLM". */
#define KERNEL_MODEL_SLOT_MAGIC 0x4D4C504Cu

/**
 * @enum ModelSlotState_t
 * @brief What the slot holds.
 */
typedef enum {
    MODEL_SLOT_EMPTY = 0, /**< No module with the suffix was passed. */
    MODEL_SLOT_MALFORMED, /**< A module is there and is not a weights image. */
    MODEL_SLOT_LOADED,    /**< A module is there and its header holds up. */
} ModelSlotState_t;

/**
 * @struct ModelSlot_t
 * @brief The result of probing the slot.
 */
typedef struct {
    ModelSlotState_t state; /**< Empty, malformed or loaded. */
    const uint8_t *bytes;   /**< Kernel-virtual base, NULL unless loaded. */
    uint32_t size;          /**< Module length in bytes. */
    uint32_t magic;         /**< First word read, for diagnosing a malformed image. */
} ModelSlot_t;

/**
 * @brief Looks for a weights module and checks its header.
 *
 * Only the header: the extent of every tensor is checked by the reader that knows
 * the shape, and duplicating that arithmetic here would put the layout in two places.
 *
 * @note The magic is assembled byte by byte rather than read through a uint32_t pointer:
 *       a module lands wherever the loader put it, and a four-byte load from an odd address
 *       is undefined even where the hardware would tolerate it.
 *
 * @param out Receives the finding.
 * @return true when the slot is loaded.
 */
bool kernel_model_slot_probe(ModelSlot_t *out);

/**
 * @brief A word for the state, for a boot line.
 * @param slot The finding.
 * @return "empty", "malformed" or "loaded".
 */
const char *kernel_model_slot_state_text(const ModelSlot_t *slot);

/**
 * @brief Turns read-only every whole page of a loaded slot's image, once the reader has checked its signatures and
 *        borrows its bytes: a write there would change a weight already checked.
 *
 * @param slot A loaded slot.
 * @return Pages turned read-only; 0 for a slot that is not loaded, an image that holds no whole page or would end
 *         past 4 GiB, or a processor that does not enforce read-only pages in ring 0.
 */
uint32_t kernel_model_slot_protect(const ModelSlot_t *slot);

/**
 * @brief Writes one `[LPLTLM] model` record: the slot's state, where the mind's weights came from, what reading the
 *        image found, the image's bytes and the pages turned read-only.
 *
 * @param slot            The slot the mind booted from.
 * @param origin          "seed", "module_in_place" or "module_copied".
 * @param reading         What the reading found, by its name, or "none".
 * @param protected_pages Pages kernel_model_slot_protect turned read-only.
 */
void kernel_model_slot_report(const ModelSlot_t *slot, const char *origin, const char *reading,
                              uint32_t protected_pages);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_AI_MODEL_SLOT_H */
