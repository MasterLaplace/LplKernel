#ifndef KERNEL_CORE_KERNEL_SMP_H
#define KERNEL_CORE_KERNEL_SMP_H

#include <kernel/drivers/serial.h>

/**
 * @brief Coordinates the SMP startup sequence for all discovered application processors (APs).
 *
 * This function handles bootstrapping, INIT/SIPI sequence dispatch, trampoline installation,
 * and telemetry/diagnostics for bringing secondary cores online safely.
 *
 * @param com1 Pointer to the primary serial interface for diagnostic output.
 */
extern void kernel_smp_try_start_discovered_aps(Serial_t *com1);

#endif /* KERNEL_CORE_KERNEL_SMP_H */
