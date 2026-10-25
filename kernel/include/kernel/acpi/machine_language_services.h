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
 * @file machine_language_services.h
 * @brief What the operating system services AML calls hold: the Windows identity asked, the time an
 *        evaluation requested, the mutexes it holds, and the Notify ring.
 *
 * Part of layer L5 of the specification (docs/acpi/specification.md, §3.7, §5.2, §5.4, §5.5 and
 * §6.1). One owner runs AML, one evaluation at a time, so nothing here takes a lock: an AML mutex is
 * a count the evaluation holds, released when the evaluation ends, and a Notify waits in a ring until
 * the evaluation that raised it is over. Time is counted as AML requests it, never as a clock
 * measures it, so a budget ends the same way wherever it runs.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-26
 **************************************************************************/

#ifndef KERNEL_ACPI_MACHINE_LANGUAGE_SERVICES_H
#define KERNEL_ACPI_MACHINE_LANGUAGE_SERVICES_H

#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TIME_REQUESTED                                 \
    30000000u /**< Microseconds Sleep and Stall may request in one evaluation: 30 s (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_HELD_MUTEXES                                   \
    32u /**< Mutexes one evaluation holds at once (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NOTIFICATIONS                                      \
    64u /**< Entries of the Notify ring (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_WINDOWS_XP                                         \
    2u /**< The Windows version "Windows 2001" names, counted from 1 for "Windows 2000": from it on, more ports are    \
            protected (§5.3). */

/**
 * @brief A mutex an evaluation holds, and how many times it acquired it.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageHeldMutex {
    uint32_t node;        /**< The mutex's node. */
    uint32_t depth;       /**< Acquisitions not released yet, at least 1. */
    uint8_t level_before; /**< The sync level its first acquisition found, which its release gives back. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageHeldMutex_t;

/**
 * @brief A Notify waiting to be delivered once its evaluation is over (§3.7).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageNotification {
    uint32_t node;  /**< The device, processor or thermal zone notified. */
    uint32_t value; /**< The notification value. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageNotification_t;

/**
 * @brief What the operating system services of a load hold and counted.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceMachineLanguageServices {
    uint8_t windows_version;                  /**< The newest Windows string \_OSI was asked for, counted from 1
                                                   for "Windows 2000"; 0 before any. */
    uint8_t sync_level;                       /**< The current sync level: the highest of the mutexes the
                                                   evaluation acquired and of the Serialized methods it runs. */
    uint32_t operating_system_queries;        /**< Calls of \_OSI. */
    uint32_t operating_system_queries_denied; /**< Of those, the ones answered False. */
    uint64_t time_requested;                  /**< Microseconds Sleep and Stall requested in the current
                                                   evaluation. */
    uint64_t time_requested_peak;             /**< The most one evaluation requested. */
    uint64_t sleep_milliseconds;              /**< Milliseconds Sleep requested, over every evaluation. */
    uint64_t stall_microseconds;              /**< Microseconds Stall requested, over every evaluation. */
    uint64_t waited_milliseconds;             /**< Milliseconds the host's sleep said it waited, Stall's long
                                                   waits included. */
    uint64_t stall_waited_microseconds;       /**< Microseconds the host's busy-waits said they waited. */
    uint32_t sleeps;                          /**< Sleeps run. */
    uint32_t stalls;                          /**< Stalls run. */
    uint32_t long_stalls;                     /**< Of those, the ones beyond 100 µs. */
    uint32_t stalls_slept;                    /**< Of those, the ones beyond 255 µs, made a sleep. */
    uint32_t timer_reads;                     /**< Reads of Timer. */
    uint32_t held_count;                      /**< Mutexes the current evaluation holds. */
    uint32_t held_peak;                       /**< The most one evaluation held at once. */
    uint32_t acquisitions;                    /**< Acquires that took or nested a mutex. */
    uint32_t acquisitions_ignored;            /**< Acquires below the current sync level, ignored. */
    uint32_t releases_ignored;                /**< Releases of a mutex not held, or of another SyncLevel than
                                                   the one acquired last, ignored. */
    uint32_t released_at_end;                 /**< Mutexes still held when their evaluation ended. */
    uint32_t signals;                         /**< Signals of an Event. */
    uint32_t notifications;                   /**< Notifies raised, those the full ring dropped included. */
    uint32_t notifications_dropped;           /**< Notifies the full ring could not take. */
    uint32_t notifications_delivered;         /**< Notifies taken from the ring. */
    uint32_t notification_first;              /**< The ring's oldest entry. */
    uint32_t notification_count;              /**< Entries the ring holds. */
    uint32_t notification_peak;               /**< The most it held at once. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeldMutex_t
        held[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_HELD_MUTEXES]; /**< The mutexes held, in
                                                                                                acquisition order. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNotification_t
        ring[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NOTIFICATIONS]; /**< The Notify ring. */
} AdvancedConfigurationAndPowerInterfaceMachineLanguageServices_t;

#endif /* KERNEL_ACPI_MACHINE_LANGUAGE_SERVICES_H */
