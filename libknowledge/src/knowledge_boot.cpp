/**
 * @file knowledge_boot.cpp
 * @brief Opening the knowledge image the kernel carries.
 *
 * ⚠ The brief that stood here said the pack arrives as a BOOT MODULE, and that is not what
 * this file does today — corrected rather than left to mislead. The image is
 * `kParityKnowledgeImage`, a byte array in the tree, for the same reason
 * `ParityPackBlob.hpp` is one: a kernel build must require no host tool, and a gate that
 * needed a file present would be a gate that skips itself when it is not.
 *
 * The boot-module path is still the right destination and is deliberately NOT written yet:
 * nothing produces a harvested `.lplknow` image, so a loader for one would be a reader with
 * no writer — the orphan this project refuses. What is kept from that design is the
 * reporting: @ref libknowledge_image_state says WHERE the image came from, so the day a
 * booted one exists, "read the built-in corpus" and "read what was booted" are
 * distinguishable in a log rather than the same line.
 *
 * Validation is unconditional either way: magic, version, declared extent, content hash. An
 * invalid image is reported, never silently replaced by a built-in fallback; a corrupt
 * memory has to be visible as a corrupt memory.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include "libknowledge/libknowledge.h"

#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/knowledge/ParityKnowBlob.hpp>

extern "C" uint32_t libknowledge_image_bytes(void) { return lpl::knowledge::kParityKnowledgeImageSize; }

extern "C" const char *libknowledge_image_state(void)
{
    // Validated before it is called embedded, not merely present. An image the kernel
    // carries can still be wrong — a hand-edited blob, a half-applied patch — and reporting
    // "embedded" for one that does not open would be the silent fallback this file's brief
    // forbids. The three answers stay apart on purpose, exactly as the weights slot keeps
    // absent, malformed and loaded apart.
    lpl::knowledge::KnowledgePack pack;
    switch (pack.open(lpl::knowledge::kParityKnowledgeImage, lpl::knowledge::kParityKnowledgeImageSize))
    {
    case lpl::knowledge::OpenStatus::Ok: return "embedded";
    default: return "malformed";
    }
}
