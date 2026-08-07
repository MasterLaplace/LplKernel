#ifndef KERNEL_CORE_CONSOLE_H
#define KERNEL_CORE_CONSOLE_H

#include <kernel/drivers/serial.h>

/**
 * @brief Whether this image carries an interactive command surface.
 *
 * A shell is the one facility an immutable, API-only node is defined by NOT
 * having: no arbitrary command means no configuration drift between two boots of
 * the same image, and nothing interactive to reach. The console is a development
 * convenience, so it is compiled in while developing (`KERNEL_CONSOLE=1`, the
 * default of both build paths) and compiled out of a production image
 * (`xmake -m release`).
 */
#if defined(LPL_KERNEL_ENABLE_CONSOLE)
#    define KERNEL_CONSOLE_IS_COMPILED_IN true
#else
#    define KERNEL_CONSOLE_IS_COMPILED_IN false
#endif

/**
 * @brief Enters the interactive kernel console loop.
 *
 * Starts a non-blocking loop handling keyboard and serial input
 * to process basic kernel diagnostic commands.
 *
 * The symbol exists in both configurations, so callers need no conditional. When
 * the surface is compiled out, the function reports its absence and idles rather
 * than returning: returning would fall out of kernel_main into the destructor,
 * which prints a panic line for what is a normal end of boot.
 *
 * @param com1 Pointer to the primary serial interface.
 */
/**
 * @brief Report whether this image carries an interactive command surface.
 *
 * Emitted at boot rather than from the loop, because the loop only runs on the
 * profile that has no engine to run instead — so a report from inside it would be
 * missing from exactly the images anyone would want to check.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void kernel_console_report_surface(Serial_t *com1);

extern void kernel_console_run_interactive_loop(Serial_t *com1);

#endif /* KERNEL_CORE_CONSOLE_H */
