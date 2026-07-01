#ifndef KERNEL_CORE_LIBENGINE_SMOKE_H
#define KERNEL_CORE_LIBENGINE_SMOKE_H

#include <kernel/drivers/serial.h>

/**
 * @brief Runs the full libengine cross-target smoke/demo battery (P0..P6).
 *
 * Extracted from kernel_main so the boot path stays readable and so the whole
 * battery can be compiled out of a release/production image (it is only invoked
 * when LPL_KERNEL_ENABLE_SMOKE_TESTS is defined). Each block folds deterministic
 * engine results and prints them over serial for byte-for-byte comparison
 * against the Linux/xmake oracle (the HARD determinism contract).
 *
 * Covers: the C++ constructor self-test, P0 determinism, P1 arena/ECS/scheduler/
 * physics, P2 HAL, P3 render, P4 image/scene + the lplplugin_initialize boot
 * facade, P5 render + render-present, P6 advanced rendering, and the P4 image
 * present that paints the final 2D scene onto the scanout.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void libengine_smoke_run_all(Serial_t *com1);

#endif /* KERNEL_CORE_LIBENGINE_SMOKE_H */
