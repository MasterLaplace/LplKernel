#ifndef KERNEL_CORE_SPLASH_H
#define KERNEL_CORE_SPLASH_H

#include <stdint.h>

/**
 * @brief Initializes the splash screen loading bar state.
 * @param total_steps The absolute total number of segments for the progress bar.
 */
extern void kernel_splash_initialize(uint32_t total_steps);

/**
 * @brief Updates the splash screen with a new loading step name.
 * @param step_name The short description of the initialization piece.
 */
extern void kernel_splash_update(const char *step_name);

/**
 * @brief Finishes the loading sequence, completely clears the terminal
 *        and resets the cursor for the final kernel welcome message.
 */
extern void kernel_splash_finish(void);

#endif /* KERNEL_CORE_SPLASH_H */
