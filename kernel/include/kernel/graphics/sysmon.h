#ifndef KERNEL_GRAPHICS_SYSMON_H
#define KERNEL_GRAPHICS_SYSMON_H

#include <kernel/drivers/serial.h>

/**
 * @brief Real-time, purely-visual system monitor for the client/graphics mode.
 *
 * A "graphical htop": instead of text it renders, every frame, a live 2D view
 * of what the machine is actually doing — physical-memory / buddy-allocator
 * occupancy, the kernel heap size-classes, the frame-arena / pool / stack
 * allocators, pinned (GPU) memory, the SPSC transfer ring, and the PCI bus —
 * as animated gauges, heatmaps and flowing data particles.
 *
 * It samples the kernel telemetry counters each frame, animates the deltas as
 * moving particles (so you literally see data move between subsystems), draws
 * to the linear framebuffer, and loops until a key is pressed. Replaces the old
 * static kernel_smoke_test_run_graphics_demo.
 *
 * Pure observability: it reads live, non-authoritative counters and is NOT part
 * of the deterministic engine contract.
 *
 * @param com1 Pointer to the primary serial interface (status logging).
 */
extern void kernel_sysmon_run(Serial_t *com1);

#endif /* KERNEL_GRAPHICS_SYSMON_H */
