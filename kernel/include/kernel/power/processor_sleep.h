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
 * @file processor_sleep.h
 * @brief Sleeping between buffers instead of spinning.
 *
 * A satellite is silent most of its life. `HLT` alone wakes late; the monitor/wait pair
 * lets the processor sleep and be woken by a write to a watched address — which is
 * precisely what a device pushing an audio buffer does. This is the difference between
 * a node that idles at a few watts and one that idles at twenty.
 *
 * Everything here is accounted, and that is not decoration. "This profile costs
 * nothing when idle" is a claim about energy, and a claim about energy that is not
 * measured is a wish. The counters below are what let the satellite profile print a
 * duty cycle instead of a promise.
 *
 * The pair also has a discipline that a wrapper cannot enforce and a caller must
 * not skip: between arming the monitor and sleeping, re-check the condition. If the
 * device wrote in that window the monitor is already spent, and the sleep would be
 * waiting for a wake-up that has already happened. @ref processor_sleep_until_write
 * takes the condition as an argument for exactly this reason.
 *
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-08-05
 **************************************************************************/

#ifndef KERNEL_POWER_PROCESSOR_SLEEP_H
#define KERNEL_POWER_PROCESSOR_SLEEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MWAIT's hint index for C1, the shallowest: clock stopped, power maintained. Passing
 * this is what the kernel did before any of the enumeration below existed, and it is
 * still the default, because a deeper state costs wake latency that the caller — not
 * this module — is the one able to afford or not.
 */
#define PROCESSOR_SLEEP_HINT_C1 0u

/**
 * CPUID.05H:EDX carries eight nibbles, C0* through C7*, so the deepest hint it can
 * enumerate targets C7*. Note MWAIT reserves the hint value 0b1111 for C0, which is why
 * the useful range stops well short of the field's width.
 */
#define PROCESSOR_SLEEP_HINT_MAX 6u

/**
 * What kernel_processor_sleep_published_duty_cycle_permille() reads while no session has been
 * closed since the last one opened. Outside 0 to 1000 on purpose: every value in that range is
 * a duty cycle somebody could measure, zero included.
 */
#define KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED 0xFFFFFFFFu

/**
 * @enum ProcessorSleepMode_t
 * @brief How the processor was actually put to sleep.
 */
typedef enum {
    PROCESSOR_SLEEP_NONE = 0, /**< It was not: the condition was already true. */
    PROCESSOR_SLEEP_HALT,     /**< `HLT`, woken by an interrupt. */
    PROCESSOR_SLEEP_MONITOR,  /**< `MONITOR`/`MWAIT`, woken by a write or an interrupt. */
} ProcessorSleepMode_t;

/**
 * @brief Probes what the processor offers, zeroes the accounting and opens a session.
 *
 * @details The session runs until kernel_processor_sleep_close_session(); until then nothing
 *          is published for a check to judge.
 *
 * MONITOR/MWAIT is advertised by CPUID leaf 1, ECX bit 3. Probed rather than
 * assumed: it is absent on early processors and, more relevantly here, absent from
 * QEMU's default model — so a kernel that executed MWAIT unconditionally would take
 * an invalid-opcode fault on the machine it is developed on.
 *
 * @note A processor that advertises the pair but reports a zero line size in CPUID leaf 5
 *       is treated as lacking it: it is not one to trust with a sleep that only a write
 *       can end, and the line size is not something to guess at.
 */
void kernel_processor_sleep_initialize(void);

/**
 * @brief Starts the counts of sleeps, halts and skips, the time asleep and awake, and the wake-ups attributed to each
 *        vector (@ref kernel_wakeup_accounting_reset) afresh, keeping what was detected and the hint granted: a
 *        session that slept to set itself up counts only what its loop does.
 */
void kernel_processor_sleep_begin_accounting(void);

/**
 * @brief Does this processor implement the monitor/wait pair?
 * @return true when CPUID advertises it.
 */
bool kernel_processor_sleep_has_monitor(void);

/**
 * @brief Whether this processor implements the monitor/wait pair, read from CPUID without arming the
 *        module: every sleep keeps the instruction it had.
 *
 * @return What kernel_processor_sleep_has_monitor answers once kernel_processor_sleep_initialize has
 *         run: true when CPUID leaf 1 advertises the pair and leaf 5 gives a non-zero line size.
 */
bool kernel_processor_sleep_monitor_implemented(void);

/**
 * @brief Smallest cache line the monitor can watch, in bytes.
 * @return The size, or 0 when the pair is unavailable.
 */
uint32_t kernel_processor_sleep_monitor_line_bytes(void);

/**
 * @brief Which MWAIT hints this CPUID word enumerates, as a bit per hint.
 *
 * @details Pure, and separate from the live CPUID read so a test can hand it words no
 *          emulator produces. The offset it encodes is the one worth getting right:
 *          CPUID.05H:EDX reports C1* in bits 7:4 and C2* in bits 11:8, while MWAIT's
 *          hint 0 targets C1 and 1 targets C2 — so hint @c h is enumerated exactly when
 *          nibble @c h+1 is non-zero. A bit mask rather than a count because real
 *          processors leave GAPS, offering C1 and C6 and nothing between; a count would
 *          claim the missing ones exist.
 *
 * @param leaf5_edx The value CPUID leaf 5 returned in EDX.
 * @return Bit @c h set when hint @c h is enumerated. Zero means none is.
 */
uint32_t kernel_processor_sleep_enumerated_hints(uint32_t leaf5_edx);

/**
 * @brief Which of the enumerated hints a sleep may take: every one where the local timer always runs, and C1's
 *        alone where it may stop.
 *
 * @details The local timer owns the tick in every profile and arms every tickless sleep's wake-up, so a C-state
 *          that stops it stops both; C1 never does, whatever the processor. Pure, like
 *          @ref kernel_processor_sleep_enumerated_hints, so that a test hands it what no emulator gives.
 *
 * @param enumerated        What @ref kernel_processor_sleep_enumerated_hints gave.
 * @param timer_always_runs What @ref advanced_pic_timer_backend_always_runs says.
 * @return The hints a sleep may take, a bit per hint.
 */
uint32_t kernel_processor_sleep_usable_hints(uint32_t enumerated, bool timer_always_runs);

/**
 * @brief Which MWAIT hints a sleep may take on this processor: the usable ones of those it enumerates.
 * @return The mask, or zero when the pair is unavailable.
 */
uint32_t kernel_processor_sleep_available_hints(void);

/**
 * @brief Can an unmasked interrupt break an MWAIT on this processor?
 *
 * @details CPUID.05H:ECX bit 1. Setting MWAIT's extension bit without it raises #GP, so
 *          this is a guard and not a capability report.
 *
 * @return true when the extension may be used.
 */
bool kernel_processor_sleep_has_interrupt_break(void);

/**
 * @brief Asks for a sleep depth, clamped to what the processor enumerates and its local timer allows.
 *
 * @details Asking is not getting: a request past the usable hints, those the processor enumerates and, without
 *          a local timer that always runs, C1's alone (@ref kernel_processor_sleep_usable_hints), is lowered to
 *          the deepest usable hint below it and counted, rather than passed through to raise a fault, to land
 *          somewhere unintended, or to stop the timer that wakes the core. What a hint costs in wake latency is
 *          NOT in CPUID: it lives in ACPI `_CST`, which the ACPI layer publishes per CPU
 *          (kernel/acpi/processor_idle.h) and no sleep reads yet (#298), so choosing a depth stays a decision of
 *          the caller that knows its own deadline, never a derivation.
 *
 * @param hint Desired hint index; @ref PROCESSOR_SLEEP_HINT_C1 is the floor.
 * @return true when the request was granted exactly as asked.
 */
bool kernel_processor_sleep_request_hint(uint32_t hint);

/**
 * @brief The hint MWAIT is currently given.
 * @return The hint index.
 */
uint32_t kernel_processor_sleep_active_hint(void);

/**
 * @brief The depth a sleep until an interrupt enters: the active hint where MWAIT is used, C1 where the core halts.
 * @return That hint index.
 */
uint32_t kernel_processor_sleep_depth(void);

/**
 * @brief Times a requested depth was lowered to what the processor enumerates.
 * @return The count.
 */
uint32_t kernel_processor_sleep_clamped_count(void);

/**
 * @brief Sleeps until @p watched changes, or an interrupt arrives.
 *
 * The value is re-read after arming the monitor and before sleeping, which closes
 * the race the pair is famous for: if the device wrote between the caller's own check
 * and the MONITOR, the monitor is already spent and MWAIT would sleep waiting for
 * something that has already happened. Falls back to `HLT` where the pair is absent —
 * correct, just less precise: a halted core still wakes on the timer interrupt, it
 * simply cannot be woken by a device's DMA alone.
 *
 * @details Interrupts are masked from before the re-read to the sleep, which enables them in the same step (STI then
 *          HLT or MWAIT): an interrupt whose handler writes @p watched after the re-read ends the sleep instead of
 *          being handled just before it. The interrupt flag is given back as it was found.
 *
 * @param watched  Address to watch; typically a driver's write index.
 * @param expected Value that means "nothing new yet".
 * @return How the processor was put to sleep, if at all.
 */
ProcessorSleepMode_t processor_sleep_until_write(const volatile uint32_t *watched, uint32_t expected);

/**
 * @brief Sleeps until the next interrupt, at the depth last granted.
 *
 * For an idle loop with nothing to watch. Where MONITOR/MWAIT exists the monitor is
 * armed on a line nothing writes, so only an interrupt ends the wait, as with HLT, but
 * at the hint @ref kernel_processor_sleep_request_hint granted; without the pair it
 * halts in C1 and is counted as a halt. MWAIT may also exit for implementation-defined
 * reasons with no interrupt at all, and such a wake is counted as unattributed rather
 * than credited to a source that did not cause it.
 *
 * @details It may be entered with interrupts masked, and that is how a caller that arms a wake-up and then sleeps on
 *          it loses none: an interrupt that became pending since the caller masked them, its own timer's included,
 *          ends the sleep and is handled before the call returns, since the sleep enables interrupts in the same step
 *          (STI then HLT or MWAIT, Intel SDM vol. 3A, §7.8.1). The interrupt flag is given back as it was found.
 */
void processor_sleep_until_interrupt(void);

/**
 * @brief Whether what a sleeper waits for has happened; called with interrupts masked.
 * @param context What the sleeper gave @ref processor_sleep_unless.
 * @return true when the wait is over.
 */
typedef bool (*ProcessorSleepCondition_t)(void *context);

/**
 * @brief Sleeps until the next interrupt unless @p done says the wait is over, asked with interrupts masked.
 *
 * For a loop that waits on what an interrupt handler changes, a tick count or a flag: the handler that changes it
 * after @p done read it ends the sleep instead of running just before it, which would leave the sleep waiting for the
 * interrupt after, a tick too late, or with the tick stopped forever. It is counted and attributed as
 * @ref processor_sleep_until_interrupt's sleep is, and a wait already over as a skip, but it sleeps in C1 whatever
 * depth was granted: such a wait is a tick at most, and a deeper state could stop the local timer whose tick ends it
 * on a processor without ARAT (#298).
 *
 * @param done    Asked once, with interrupts masked; not NULL.
 * @param context What @p done receives.
 * @return What @p done said: true when no sleep was entered.
 */
bool processor_sleep_unless(ProcessorSleepCondition_t done, void *context);

/**
 * @brief Times the processor was put to sleep.
 * @return The count, both modes together.
 */
uint32_t kernel_processor_sleep_count(void);

/**
 * @brief Times a sleep was skipped because the condition was already true.
 *
 * The number that says whether the watch is set on the right address. A node that
 * never actually sleeps is a node whose condition is always already true — which
 * looks identical, from the outside, to one that sleeps perfectly.
 *
 * @return The count.
 */
uint32_t kernel_processor_sleep_skipped_count(void);

/**
 * @brief Times the fallback was taken because the pair is unavailable.
 * @return The count.
 */
uint32_t kernel_processor_sleep_halt_count(void);

/**
 * @brief Cycles spent asleep.
 * @return The total, from the timestamp counter.
 */
uint64_t kernel_processor_sleep_asleep_cycles(void);

/**
 * @brief Cycles spent awake between sleeps.
 *
 * Measured from the end of one sleep to the start of the next, so it counts the work
 * and not the accounting. Before the first sleep there is nothing to measure and this
 * stays zero.
 *
 * @return The total.
 */
uint64_t kernel_processor_sleep_awake_cycles(void);

/**
 * @brief Share of accounted time the processor was awake, in thousandths.
 *
 * The satellite profile's headline number. Nothing accounted yet reads as fully
 * awake rather than fully asleep: a counter that claimed perfect efficiency before
 * measuring anything is the one answer a power figure must never give by default.
 *
 * @return Awake over awake plus asleep, 0 to 1000.
 */
uint32_t kernel_processor_sleep_duty_cycle_permille(void);

/**
 * @brief Closes the session kernel_processor_sleep_initialize() opened and publishes its duty cycle.
 *
 * @details A duty cycle is a ratio over a window, and a window that has not ended is not one:
 *          two sleeps into a session, a single long wake can put the ratio anywhere. So the
 *          ratio is published when the owner of the session says it is over, and not after
 *          each accounting. Sleeps taken outside a session (a smoke that waits for a write, the
 *          console waiting for a key) still add to the totals, and never reach the published
 *          value.
 */
void kernel_processor_sleep_close_session(void);

/**
 * @brief The duty cycle of the last closed session, in one word an interrupt can read whole.
 *
 * @details The awake and asleep totals are 64-bit, and an i686 writes them in two halves: a
 *          reader in interrupt context could see one half updated and not the other, and divide
 *          a torn total. The reconciler reads from the timer interrupt, so it reads this word.
 *
 * @note Unlike kernel_processor_sleep_duty_cycle_permille(), a session that accounted nothing
 *       is not reported as fully awake: a check that holds the duty cycle to a ceiling has to
 *       tell "never slept" from "never measured".
 *
 * @return Per mille awake, or KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED when no session has been
 *         closed since the last kernel_processor_sleep_initialize(), or the one closed accounted
 *         no time.
 */
uint32_t kernel_processor_sleep_published_duty_cycle_permille(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_PROCESSOR_SLEEP_H */
