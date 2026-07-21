#ifndef KERNEL_TESTING_SMOKE_BATCH_H
#define KERNEL_TESTING_SMOKE_BATCH_H

#include <kernel/drivers/serial.h>

/**
 * @brief Runs all enabled runtime initialization smoke tests.
 *
 * This function consolidates the execution of individual subsystem
 * diagnostics and validations after the kernel is fully loaded.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void smoke_batch_run_initialization_tests(Serial_t *com1);

/**
 * @brief Runs all enabled post-boot and exception smoke tests.
 *
 * This function invokes testing for exceptions (#PF, #GP, etc.) and
 * other interactive demonstrations.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void smoke_batch_run_post_boot_tests(Serial_t *com1);

#endif /* KERNEL_TESTING_SMOKE_BATCH_H */
