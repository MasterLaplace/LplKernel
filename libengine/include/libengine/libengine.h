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
 * @file libengine.h
 * @brief C-callable facade for the freestanding LplPlugin engine module.
 *
 * libengine.a is the LplPlugin engine compiled -ffreestanding into the kernel
 * image (the C++ sibling of libk.a). This header is the ONLY surface the pure-C
 * kernel includes; it must stay free of C++ and of any lpl/ engine type.
 *
 * What lives here, and nothing else should be added lightly:
 *   - libengine_client_app_run / libengine_server_app_run: the real entries. They
 *     boot a real lpl::engine::Engine with an injected platform and World, exactly
 *     as apps/client/main.cpp does on Linux. The kernel passes no engine state and
 *     holds no game logic.
 *   - libengine_test_suite_count / libengine_test_run: every LPL_TEST linked in, the
 *     engine's and those of the mind and the memory, which the kernel's runner reports
 *     after its own, in debug images only.
 *   - libengine_identity_telemetry: which LplPlugin was compiled in, printed at boot.
 *
 * There is deliberately no C simulation facade (init/step/render/entity_count):
 * driving a sim from the kernel in C was scaffolding, and the World seam replaced
 * it.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-06-25
 **************************************************************************/

#ifndef _LIBENGINE_H
#define _LIBENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Writes @p length characters of @p text to the port @p context stands for.
 */
typedef void (*libengine_test_write_t)(void *context, const char *text, size_t length);

/**
 * @brief What the LPL_TEST tests came to, which the kernel adds to its own totals.
 */
typedef struct {
    uint32_t passed;   /**< Tests whose every claim held. */
    uint32_t failed;   /**< Tests with a claim that did not hold, or with none. */
    uint32_t skipped;  /**< Tests that did not run. */
    uint32_t checks;   /**< Claims checked. */
    uint32_t selected; /**< Tests the selection named. */
} libengine_test_totals_t;

/**
 * @brief Number of suites the LPL_TEST tests make, for the kernel's KTAP plan.
 */
uint32_t libengine_test_suite_count(void);

/**
 * @brief Runs every LPL_TEST linked into the image (in debug images only: the engine's, and the
 *        mind's and the memory's when they are linked) as KTAP suites numbered from
 *        @p first_suite_number, which follow the kernel's own.
 *
 * @param write              Writes the report.
 * @param context            Handed to @p write.
 * @param selection          The value of `lpl.test=`, or NULL to run every test.
 * @param first_suite_number KTAP number of the first suite.
 * @return What the tests came to.
 */
libengine_test_totals_t libengine_test_run(libengine_test_write_t write, void *context, const char *selection,
                                           uint32_t first_suite_number);

/**
 * @brief Which LplPlugin this libengine was compiled from, as one telemetry record, for
 *        instance `[LPLTLM] engine version=0.2.0 commit=ac00daf`.
 *
 * @return A string with static storage, ending in a newline. The version comes from
 *         LplPlugin's lplplugin/config.h, the commit is stamped by the build.
 */
const char *libengine_identity_telemetry(void);

/**
 * @brief Kernel client entry point, the freestanding mirror of apps/client/main.cpp.
 *
 * @details Takes the cartridge the way the engine test `cartridge` does. The world the viewer
 *          draws is then the world the .lplscene document describes, decoded by the same
 *          freestanding reader — not a pipeline the cartridge cannot reach. The fallback is
 *          the VIEWER's world, not the parity gate's: the gate needs a 24x24 world small
 *          enough to fold in a boot test, the demo needs one worth looking at. Sharing
 *          one blob meant the published browser demo — a deployment whose runner has no
 *          lpl-bake, so no cartridge — showed the test world.
 *
 *          There is almost nothing behind this entry, and that is the point. Decoding a
 *          cartridge, sizing the budgets for the machine, constructing the Engine and
 *          running init/run/shutdown are the same on every host, so they live in
 *          engine::bootGame and engine::HostProfile. What remains in client_app.cpp is what
 *          only the KERNEL can say: which platform seam to inject, where the cartridge bytes
 *          are, and which World the client profile runs.
 *
 * @note The translation unit is C++ exposed through this one extern "C" symbol, because
 *       kernel.c is C and the entry must be built with the engine's C++23 + SSE
 *       determinism flags.
 *
 * @param pack_bytes Bytes of a .lplpak boot module to run THAT game, or NULL to fall back
 *                   to the viewer's pack compiled into the image.
 * @param pack_size  Size of @p pack_bytes, 0 with NULL.
 *
 * Blocks until the World requests shutdown.
 */
extern void libengine_client_app_run(const void *pack_bytes, uint32_t pack_size);

/**
 * @brief Kernel server entry point, the freestanding mirror of apps/server/main.cpp.
 *
 * @details Same shape as the client entry point, and for the same reason: everything that
 *          is host-independent (budgets, engine construction, the loop) is in
 *          engine::bootGame and engine::HostProfile. What server_app.cpp adds is the
 *          platform seam, the tick rate a server wants, and which World it hosts. Blocks
 *          until the World requests shutdown.
 */
extern void libengine_server_app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* _LIBENGINE_H */
