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
 * @file log.h
 * @brief The lines the ACPI layer writes to its host's log, and the levels a build compiles out.
 *
 * A line is an event word, the name of the counter it stands beside, then fields `key=value` whose values
 * hold no space and no `=`, such as `checksum_bad_loaded table=DSDT/MADEUPML` or `debug_stores kind=Integer ...`. It
 *goes to the host's log call at a level (§3.3). A build keeps the levels from
 *ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_ERROR to ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_BUILD_LEVEL,
 *debug for a debug build, warning for a release one, and compiles out the others (O16): below its level a build
 *compiles neither the level's log function, advanced_configuration_and_power_interface_log_debug for a release build,
 *nor the code that builds its lines, which each call site wraps in
 *ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_AT_DEBUG. nm then finds no symbol of that function in a release object.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-06
 **************************************************************************/

#ifndef KERNEL_ACPI_LOG_H
#define KERNEL_ACPI_LOG_H

#include <kernel/acpi/host.h>
#include <kernel/acpi/tables.h>

#include <stdbool.h>
#include <stdint.h>

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)
#    define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_BUILD_LEVEL                                                 \
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_DEBUG /**< A debug build keeps every level. */
#else
#    define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_BUILD_LEVEL                                                 \
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_WARNING /**< A release build compiles out debug (O16). */
#endif

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_LINE_BYTES                                                      \
    256u /**< Bytes of a line, its NUL included: a line that would be longer ends with "...". */

/**
 * @brief A line being written: its text so far, always ended by a NUL.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceLogLine {
    char text[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_LINE_BYTES]; /**< The line. */
    uint32_t length;                                                      /**< Its characters, the NUL apart. */
    bool cut; /**< Whether a field did not fit: the line then ends with "...", and takes no more. */
} AdvancedConfigurationAndPowerInterfaceLogLine_t;

/**
 * @brief Starts a line with its event word.
 *
 * @param line  The line, emptied first.
 * @param event The event, the name of the counter the line stands beside.
 */
extern void advanced_configuration_and_power_interface_log_open(AdvancedConfigurationAndPowerInterfaceLogLine_t *line,
                                                                const char *event);

/**
 * @brief Appends the field `key=value` to a line, each character of @p value that is a space, an `=` or not
 *        printable written as `_`.
 *
 * @param line  The line.
 * @param key   The field's name.
 * @param value Its value, ended by a NUL.
 */
extern void advanced_configuration_and_power_interface_log_text(AdvancedConfigurationAndPowerInterfaceLogLine_t *line,
                                                                const char *key, const char *value);

/**
 * @brief Appends the field `key=value` to a line, @p value given by its characters, as a String or a table's
 *        identifier holds them, each that is a space, an `=` or not printable written as `_`.
 *
 * @param line       The line.
 * @param key        The field's name.
 * @param characters Its value's characters, not ended by a NUL.
 * @param count      How many there are.
 */
extern void
advanced_configuration_and_power_interface_log_characters(AdvancedConfigurationAndPowerInterfaceLogLine_t *line,
                                                          const char *key, const char *characters, uint32_t count);

/**
 * @brief Appends the field `key=0x...` to a line, @p value as @p digits upper-case hexadecimal digits.
 *
 * @param line   The line.
 * @param key    The field's name.
 * @param value  The value, of which the low 4 × @p digits bits are written.
 * @param digits Its digits, 1 to 16.
 */
extern void
advanced_configuration_and_power_interface_log_hexadecimal(AdvancedConfigurationAndPowerInterfaceLogLine_t *line,
                                                           const char *key, uint64_t value, uint32_t digits);

/**
 * @brief Appends the field `key=<decimal>` to a line.
 *
 * @param line  The line.
 * @param key   The field's name.
 * @param value The value.
 */
extern void
advanced_configuration_and_power_interface_log_unsigned(AdvancedConfigurationAndPowerInterfaceLogLine_t *line,
                                                        const char *key, uint32_t value);

/**
 * @brief Appends the field `table=<signature>/<OEM table ID>` to a line, the identifier's trailing spaces and
 *        NULs left out, and each other character that is a space, an `=` or not printable written as `_`.
 *
 * @param line   The line.
 * @param header The table's header.
 */
extern void
advanced_configuration_and_power_interface_log_table(AdvancedConfigurationAndPowerInterfaceLogLine_t *line,
                                                     const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header);

/**
 * @brief Writes a line to the host's log at ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_ERROR.
 *
 * @param host The host, whose log is called unless it is NULL.
 * @param line The line.
 */
extern void
advanced_configuration_and_power_interface_log_error(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                                                     const AdvancedConfigurationAndPowerInterfaceLogLine_t *line);

/**
 * @brief Writes a line to the host's log at ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_WARNING.
 *
 * @param host The host, whose log is called unless it is NULL.
 * @param line The line.
 */
extern void
advanced_configuration_and_power_interface_log_warning(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                                                       const AdvancedConfigurationAndPowerInterfaceLogLine_t *line);

#if ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_BUILD_LEVEL >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_DEBUG
/**
 * @brief Writes a line to the host's log at ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_DEBUG; a build
 *        below that level has no such function.
 *
 * @param host The host, whose log is called unless it is NULL.
 * @param line The line.
 */
extern void
advanced_configuration_and_power_interface_log_debug(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                                                     const AdvancedConfigurationAndPowerInterfaceLogLine_t *line);

#    define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_AT_DEBUG(call)                                              \
        call /**< Runs a call that logs at debug level: the build keeps that level. */
#else
#    define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_AT_DEBUG(call)                                              \
        ((void) 0) /**< Compiles out a call that logs at debug level, with what it would build. */
#endif

/**
 * @brief The name of a level, as a host writes it.
 *
 * @param level An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_* level.
 * @return "error", "warning" or "debug", or "unknown" for any other value.
 */
extern const char *advanced_configuration_and_power_interface_log_level_name(uint8_t level);

#endif /* KERNEL_ACPI_LOG_H */
