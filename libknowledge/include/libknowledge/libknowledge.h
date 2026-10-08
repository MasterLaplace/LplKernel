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
 * @file libknowledge.h
 * @brief C facade over the demon's memory, linked into the kernel.
 *
 * The kernel is C; the module behind this header is C++. One narrow extern "C"
 * surface keeps that boundary honest, the same way libengine.h does — plain
 * structs, no ownership crossing, no exceptions, and every entry point safe to
 * call from a context that must not block.
 *
 * What this library is FOR, stated once so nobody has to infer it: `lpl::history`
 * (inside libengine, gate P13) owns the arithmetic of doubt — trust, fusion, the
 * demotion of a contradicted claim — and trades in IDENTIFIERS. This library owns
 * the other half: the `.lplknow` image, its bounded reader, and the identity that
 * turns a canonical name into one of those identifiers. `history/Fact.hpp` wrote
 * that division down itself.
 *
 * The image the kernel carries is `kParityKnowledgeImage`, a byte array in the tree,
 * for the same reason `ParityPackBlob.hpp` is one: a kernel build must require no host
 * tool, and a gate that needed a file present would be a gate that skips itself when
 * it is not. Loading a harvested image as a boot module is the right destination and
 * is deliberately not written yet: nothing produces one, so a loader would be a
 * reader with no writer.
 *
 * Validation is unconditional either way: magic, version, declared extent, content
 * hash. An invalid image is reported, never silently replaced by a built-in fallback;
 * a corrupt memory has to be visible as a corrupt memory.
 *
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-08-05
 **************************************************************************/

#ifndef LIBKNOWLEDGE_H
#define LIBKNOWLEDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Which LplKnowledge this libknowledge was compiled from, as one telemetry record, for instance
 *        `[LPLTLM] memory version=0.1.0 commit=be3d999`.
 *
 * @return A string with static storage, ending in a newline. The version comes from
 *         LplKnowledge's lplknowledge/config.h, the commit is stamped by the build.
 */
const char *libknowledge_identity_telemetry(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBKNOWLEDGE_H */
