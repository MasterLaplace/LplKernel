#ifndef KERNEL_GRAPHICS_CLIENT_APP_H
#define KERNEL_GRAPHICS_CLIENT_APP_H

#include <kernel/drivers/serial.h>

/**
 * @brief Runs the active client application (graphics payload) to the display.
 *
 * Generic, simulation-agnostic client runtime: the kernel provides the host
 * services (HAL display surface, keyboard, clock) and drives a fixed-timestep
 * simulation / uncapped-render loop, scaling each engine frame onto the HAL
 * display surface and overlaying the frame rate. The kernel knows nothing about
 * which simulation runs — that is chosen behind the engine's @c libengine_sim_*
 * facade, so any game/simulation can be plugged in without touching the kernel.
 * Loops until a key is pressed. Requires the engine module; it is compiled out
 * when LPL_PLUGIN_UNAVAILABLE.
 *
 * @param com1 Pointer to the primary serial interface (status logging).
 */
extern void kernel_client_app_run(Serial_t *com1);

#endif /* KERNEL_GRAPHICS_CLIENT_APP_H */
