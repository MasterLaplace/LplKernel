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

#include <kernel/cpu/cpu_identity.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AdvancedConfigurationAndPowerInterfaceHost;

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
 * @brief The value MWAIT takes in EAX for a hint: the target C-state less one in bits 7:4, sub-state 0 in bits 3:0,
 *        as Linux decodes it (`MWAIT_HINT2CSTATE`, arch/x86/include/asm/mwait.h), so that hint h targets C(h+1).
 *
 * @param hint A hint index, from @ref PROCESSOR_SLEEP_HINT_C1 to @ref PROCESSOR_SLEEP_HINT_MAX.
 * @return EAX for MWAIT.
 */
static inline uint32_t kernel_processor_sleep_mwait_hint(uint32_t hint) { return (hint & 0xFu) << 4u; }

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
    PROCESSOR_SLEEP_PORT,     /**< A read of the port of a C-state `_CST` lists, woken by an interrupt. */
} ProcessorSleepMode_t;

/**
 * @brief How a sleep until an interrupt went.
 */
typedef struct ProcessorSleepOutcome {
    ProcessorSleepMode_t mode; /**< How the processor was put to sleep. */
    bool interrupted;          /**< Whether an interrupt was taken before the sleep returned. */
    uint32_t depth;            /**< The depth entered: the one asked, or the one the bus-master check demoted it to;
                                    for PROCESSOR_SLEEP_NONE, the one asked. */
} ProcessorSleepOutcome_t;

#define PROCESSOR_SLEEP_DEPTHS        7u /**< Depths a ladder holds: C1, then six deeper at most. */
#define PROCESSOR_SLEEP_DEPTH_C1      0u /**< The depth of C1, which every processor enters. */
#define PROCESSOR_SLEEP_DEPTH_DEEPEST (PROCESSOR_SLEEP_DEPTHS - 1u) /**< The deepest depth a request can name. */
#define PROCESSOR_SLEEP_NO_DEPTH      0xFFFFFFFFu                   /**< What names no depth, outside every depth. */

#define PROCESSOR_SLEEP_PORT_VERDICTS          (PROCESSOR_SLEEP_DEPTHS - 1u) /**< A verdict word per depth past C1. */
#define PROCESSOR_SLEEP_PORT_VERDICT_PORT      0x0000FFFFu                   /**< A verdict word's port. */
#define PROCESSOR_SLEEP_PORT_VERDICT_VALIDATED (1u << 16) /**< A probe found that port's read stops the processor. */
#define PROCESSOR_SLEEP_PORT_VERDICT_REFUSED   (1u << 17) /**< A probe found that it does not. */

/**
 * @enum ProcessorSleepEntry_t
 * @brief How a depth is entered.
 */
typedef enum {
    PROCESSOR_SLEEP_ENTRY_HALT = 0,         /**< `HLT`. */
    PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT = 1, /**< `MWAIT` given the depth's hint, on a line nothing writes. */
    PROCESSOR_SLEEP_ENTRY_PORT = 2,         /**< A read of the depth's port, interrupts masked (ACPI 6.6 §8.1). */
} ProcessorSleepEntry_t;

/**
 * @brief One depth of a ladder: how it is entered, and the latency `_CST` publishes for it.
 */
typedef struct ProcessorSleepDepth {
    uint32_t entry;         /**< A ProcessorSleepEntry_t. */
    uint32_t hint;          /**< MWAIT's hint index for PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT, 0 otherwise. */
    uint16_t port;          /**< The port read for PROCESSOR_SLEEP_ENTRY_PORT, 0 otherwise. */
    bool latency_published; /**< Whether the processor's CPU slot publishes a latency for it. */
    bool bus_master_check;  /**< Whether a sleep checks BM_STS before entering it: a port state of type 3. */
    uint32_t latency;       /**< That latency, in microseconds; 0 when none is published. */
} ProcessorSleepDepth_t;

/**
 * @enum ProcessorSleepPorts_t
 * @brief Whether the port states of the processor's CPU slot are the depths past C1, and why not.
 */
typedef enum {
    PROCESSOR_SLEEP_PORTS_TAKEN = 0,            /**< They are. */
    PROCESSOR_SLEEP_PORTS_MONITOR_WAIT = 1,     /**< MWAIT reaches past C1, and its hints are the depths. */
    PROCESSOR_SLEEP_PORTS_NO_SLOT = 2,          /**< The processor's CPU slot publishes no state. */
    PROCESSOR_SLEEP_PORTS_TIMER_MAY_STOP = 3,   /**< The local timer, which ends every tickless sleep, may stop. */
    PROCESSOR_SLEEP_PORTS_COUNTER_MAY_STOP = 4, /**< The TSC, which times every sleep, is not invariant. */
    PROCESSOR_SLEEP_PORTS_NOT_HANDED_OVER = 5,  /**< Step 8 skipped CST_CNT: `_CST` was not handed over. */
} ProcessorSleepPorts_t;

/**
 * @brief What a ladder is built from.
 */
typedef struct ProcessorSleepLadderFacts {
    bool monitor;           /**< Whether the kernel can use MONITOR and MWAIT here. */
    bool intel;             /**< Whether the processor is Intel's, whose MWAIT hints past C1 ACPI's FFixedHW names. */
    bool bus_master_check;  /**< Whether it enters a type 3 behind the bus-master check alone
                               (@ref kernel_processor_sleep_enters_type_3_behind_the_bus_master_check). */
    uint32_t usable_hints;  /**< What @ref kernel_processor_sleep_usable_hints gives this processor. */
    bool timer_always_runs; /**< CPUID.06H:EAX[2], ARAT. */
    bool counter_invariant; /**< CPUID.80000007H:EDX[8], an invariant TSC. */
    bool control_skipped;   /**< Whether step 8 skipped CST_CNT
                               (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_SKIPPED). */
    const struct AdvancedConfigurationAndPowerInterfaceProcessorIdle *idle;    /**< The CPU slot's states, or NULL. */
    const struct AdvancedConfigurationAndPowerInterfaceFixedDescription *fadt; /**< The FADT, or NULL. */
} ProcessorSleepLadderFacts_t;

/**
 * @brief The depths one processor may sleep at, shallowest first.
 */
typedef struct ProcessorSleepLadder {
    uint32_t available; /**< A bit per depth it holds; depth 0 always. */
    uint32_t ports;     /**< A ProcessorSleepPorts_t. */
    uint32_t
        ports_refused; /**< Port states of type 2 or 3 the slot lists that are no depth, when the ports are taken. */
    uint16_t bus_master_status_ports[2]; /**< PM1a_STS and PM1b_STS, where a depth with the check reads BM_STS; 0 for
                                            one the FADT does not give, both 0 without such a depth. */
    ProcessorSleepDepth_t depths[PROCESSOR_SLEEP_DEPTHS]; /**< By depth; zero where none is available. */
} ProcessorSleepLadder_t;

/**
 * @brief Whether a processor enters a C-state of type 3 behind the bus-master check alone, its caches kept coherent
 *        without a flush and bus-master arbitration left on, as Linux's `acpi_processor_power_init_bm_check` sets
 *        bm_check and clears bm_control (v6.12 arch/x86/kernel/acpi/cstate.c).
 *
 * @details Intel's from family 0x10, or of family 6 from model 0x0F; Centaur's past family 6, or of family 6 model
 *          0x0F from stepping 0x0E; Zhaoxin's; AMD's from family 0x17. Hygon's is not: Linux v6.12 tests AMD's vendor
 *          alone. Linux also sets bm_check on a uniprocessor whatever its vendor, and leaves bm_control clear on any
 *          Intel processor whose FADT gives no PM2_CNT (v6.12 drivers/acpi/acpi_processor.c,
 * `acpi_processor_get_info`), NetBurst and the P6 family before model 0x0F among them; the kernel takes neither (#849).
 *
 * @param identity The processor's vendor and its displayed family, model and stepping.
 * @return true for those processors.
 */
bool kernel_processor_sleep_enters_type_3_behind_the_bus_master_check(const CpuIdentity_t *identity);

/**
 * @enum ProcessorSleepBusMasterReload_t
 * @brief What the last initialisation did of BM_RLD, which lets a bus master's request end C3 (ACPI 6.6 §8.1.4).
 */
typedef enum {
    PROCESSOR_SLEEP_BUS_MASTER_RELOAD_UNNEEDED = 0, /**< Its ladder holds no depth with the bus-master check. */
    PROCESSOR_SLEEP_BUS_MASTER_RELOAD_SET = 1,      /**< It holds one, and BM_RLD was set in PM1 control. */
    PROCESSOR_SLEEP_BUS_MASTER_RELOAD_UNWRITTEN =
        2, /**< It holds one, and the FADT gives no PM1 control to set it in. */
} ProcessorSleepBusMasterReload_t;

/**
 * @brief What the last initialisation did of BM_RLD: set once, and left set, where its ladder holds a depth with the
 *        bus-master check, as Linux's `acpi_processor_power_verify_c3` does.
 * @return A ProcessorSleepBusMasterReload_t; PROCESSOR_SLEEP_BUS_MASTER_RELOAD_UNNEEDED before any initialisation.
 */
uint32_t kernel_processor_sleep_bus_master_reload(void);

/**
 * @brief The name the telemetry gives what an initialisation did of BM_RLD.
 * @param reload A ProcessorSleepBusMasterReload_t.
 * @return "unneeded", "set" or "unwritten"; "unknown" for any other value.
 */
const char *kernel_processor_sleep_bus_master_reload_name(uint32_t reload);

/**
 * @brief The depths a processor may sleep at, from what it offers.
 * @details Depth 0 is C1, by MWAIT 0 where the monitor pair is usable and by HLT otherwise. Past it:
 *          - on an Intel processor whose MWAIT reaches a usable hint past C1, depth h is MWAIT hint h, gaps
 *            included, as Linux's intel_idle drives it, and the slot's port states are no depth;
 *          - on any other processor, AMD's among them, whose firmware lists its C2 as a port read, the port states
 *            @ref advanced_configuration_and_power_interface_processor_idle_ports gives the slot are depths 1 on, in
 *            `_CST`'s order, as Linux's acpi_idle drives them; only where the local timer always runs and the TSC is
 *            invariant, since a tickless sleep's wake-up and its timing rest on both (Linux marks the TSC unstable
 *            in a state past C1 without it, v6.12 `tsc_check_state`), and only where step 8 did not skip CST_CNT.
 *            The ports win there over MWAIT hints past C1, which only Intel's vendor-specific ACPI interface ties to
 *            C-states; a processor that gives no port depth keeps such hints, as it did before ports were read. The
 *            port states include the slot's type 3 where the facts say the processor enters one behind the
 *            bus-master check; each such depth has the check, and the ladder names the PM1 status registers it reads.
 *          A depth's latency is the one `_CST` publishes: its hint's
 *          (@ref advanced_configuration_and_power_interface_processor_idle_latency), or its port state's.
 * @param facts      What the processor offers.
 * @param out_ladder Receives the depths, written whole.
 */
void kernel_processor_sleep_build_ladder(const ProcessorSleepLadderFacts_t *facts, ProcessorSleepLadder_t *out_ladder);

/**
 * @brief What the processor that runs the call offers, read from CPUID, its local timer and the ACPI tables, without
 *        arming the module.
 * @param out_facts Receives the facts; its slot is the one of this processor's local APIC ID, NULL without one.
 */
void kernel_processor_sleep_read_ladder_facts(ProcessorSleepLadderFacts_t *out_facts);

/**
 * @brief The name the telemetry gives whether the port states are depths.
 * @param ports A ProcessorSleepPorts_t.
 * @return "taken", "monitor_wait", "no_slot", "timer_may_stop", "counter_may_stop" or "not_handed_over";
 *         "unknown" for any other value.
 */
const char *kernel_processor_sleep_ports_name(uint32_t ports);

/**
 * @brief The name the telemetry gives a depth's entry.
 * @param entry A ProcessorSleepEntry_t.
 * @return "halt", "mwait" or "port"; "unknown" for any other value.
 */
const char *kernel_processor_sleep_entry_name(uint32_t entry);

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
 *        vector (@ref kernel_wakeup_accounting_reset) afresh, keeping what was detected and the depth granted: a
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
 *         run: true when CPUID leaf 1 advertises the pair and leaf 5 gives a non-zero line size that
 *         @ref kernel_processor_sleep_monitor_line_fits.
 */
bool kernel_processor_sleep_monitor_implemented(void);

/**
 * @brief Smallest cache line the monitor can watch, in bytes.
 * @return The size, or 0 when the pair is unavailable.
 */
uint32_t kernel_processor_sleep_monitor_line_bytes(void);

/**
 * @brief The line a sleep that only an interrupt should end hands MONITOR, on every processor: nothing writes it, and
 *        nothing else lies in its @ref kernel_processor_sleep_idle_line_bytes, since a store anywhere in the monitored
 *        line, this processor's own after MONITOR included, ends MWAIT at once.
 * @return The line.
 */
const volatile void *kernel_processor_sleep_idle_line(void);

/**
 * @brief The bytes the idle line spans, aligned to them.
 * @return KERNEL_SPSC_RING_INTERFERENCE_SIZE.
 */
uint32_t kernel_processor_sleep_idle_line_bytes(void);

/**
 * @brief Whether a monitor whose largest line is @p largest_line_bytes watches the idle line alone.
 * @param largest_line_bytes CPUID.05H:EBX[15:0].
 * @return true when it is no larger than @ref kernel_processor_sleep_idle_line_bytes; the monitor is not used
 *         otherwise, every MWAIT depth with it.
 */
bool kernel_processor_sleep_monitor_line_fits(uint32_t largest_line_bytes);

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
 * @brief Which MWAIT hints CPUID leaves 1 and 5 enumerate, as a bit per hint, read as
 *        @ref kernel_processor_sleep_initialize reads them: none unless leaf 1 advertises the pair and leaf 5 gives a
 *        non-zero line size that @ref kernel_processor_sleep_monitor_line_fits.
 *
 * @details Pure, like @ref kernel_processor_sleep_enumerated_hints, so that a test hands it what no emulator gives.
 *
 * @param leaf1_ecx What CPUID leaf 1 returned in ECX, whose bit 3 advertises MONITOR and MWAIT.
 * @param leaf5_ebx What CPUID leaf 5 returned in EBX, whose bits 15:0 are the largest line the monitor watches.
 * @param leaf5_edx What CPUID leaf 5 returned in EDX.
 * @return What @ref kernel_processor_sleep_enumerated_hints gives @p leaf5_edx where the pair is implemented; zero
 *         otherwise.
 */
uint32_t kernel_processor_sleep_hints_of_cpuid_leaves(uint32_t leaf1_ecx, uint32_t leaf5_ebx, uint32_t leaf5_edx);

/**
 * @brief Which MWAIT hints this processor enumerates, read from CPUID without arming the module: every sleep keeps
 *        the instruction it had.
 *
 * @return What @ref kernel_processor_sleep_hints_of_cpuid_leaves gives this processor's leaves 1 and 5.
 */
uint32_t kernel_processor_sleep_hints_enumerated_by_this_processor(void);

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
 * @brief Which depths of the ladder @ref kernel_processor_sleep_initialize adopted a sleep may take, those
 *        @ref kernel_processor_sleep_refuse_depth refused left out.
 * @return A bit per depth; depth 0 always.
 */
uint32_t kernel_processor_sleep_available_depths(void);

/**
 * @brief What a depth of the adopted ladder enters.
 * @param depth     The depth.
 * @param out_depth Receives it when it is available; untouched otherwise.
 * @return Whether it is available.
 */
bool kernel_processor_sleep_depth_of(uint32_t depth, ProcessorSleepDepth_t *out_depth);

/**
 * @brief The adopted ladder, available or refused, as @ref kernel_processor_sleep_build_ladder built it.
 * @return The ladder.
 */
const ProcessorSleepLadder_t *kernel_processor_sleep_ladder(void);

/**
 * @brief Refuses a port depth whose read was found not to stop the processor, and lowers the active depth to the
 *        deepest still available below it.
 * @param depth The depth.
 * @return Whether it was refused: false for a depth that is not available or not a port read.
 */
bool kernel_processor_sleep_refuse_depth(uint32_t depth);

/**
 * @brief Depths @ref kernel_processor_sleep_refuse_depth refused since the last initialisation.
 * @return The count.
 */
uint32_t kernel_processor_sleep_refused_depth_count(void);

/**
 * @brief The active depth when it waits for a probe: a port depth, which every adoption leaves unvalidated until a
 *        probe finds its read stops the processor (kernel/power/tickless.h). Until then a sleep enters the deepest
 *        validated depth below it, so that no sleep reads a port whose read may not stop the processor, whatever
 *        stops the tick or not.
 * @return The depth, or @ref PROCESSOR_SLEEP_NO_DEPTH when the active depth is validated.
 */
uint32_t kernel_processor_sleep_depth_awaiting_probe(void);

/**
 * @brief Opens a probe of the depth awaiting one: until @ref kernel_processor_sleep_end_probe, a sleep until an
 *        interrupt enters the active depth, validated or not, and without the bus-master check, as Linux's s2idle
 *        enters C3 (v6.12 `acpi_idle_enter_s2idle`), so that each of the probe's sleeps reads the port it judges.
 */
void kernel_processor_sleep_begin_probe(void);

/**
 * @brief Closes the probe @ref kernel_processor_sleep_begin_probe opened, and validates the depth it probed, or refuses
 *        it (@ref kernel_processor_sleep_refuse_depth).
 * @param stops Whether the probe found that the depth's read stops the processor.
 */
void kernel_processor_sleep_end_probe(bool stops);

/**
 * @brief What the probes since the last initialisation found of the ports of this ladder, for the parked processors
 *        to take: per port depth judged, its port with PROCESSOR_SLEEP_PORT_VERDICT_VALIDATED or
 *        PROCESSOR_SLEEP_PORT_VERDICT_REFUSED; 0 where no probe judged.
 *
 * @details Written by @ref kernel_processor_sleep_end_probe and @ref kernel_processor_sleep_refuse_depth, cleared by
 *          @ref kernel_processor_sleep_initialize; each word is read once, whole, so that another processor reading
 *          while the bootstrap processor writes sees a word before or after, never between.
 *
 * @param out_verdicts Receives PROCESSOR_SLEEP_PORT_VERDICTS words.
 */
void kernel_processor_sleep_port_verdicts(uint32_t out_verdicts[PROCESSOR_SLEEP_PORT_VERDICTS]);

/**
 * @brief Sleeps entered by a port read since the accounting began.
 * @return The count.
 */
uint32_t kernel_processor_sleep_port_sleep_count(void);

/**
 * @brief The depth a sleep enters once the bus-master check read PM1 status @p status before depth @p depth.
 *
 * @details A depth with the check is entered while BM_STS is clear; while it is set, a bus master was active since
 *          the last check, and the sleep takes instead the deepest depth below it that @p allowed names and that has
 *          no check, C1 when none does, as Linux's `acpi_idle_enter_bm` demotes to its safe state (v6.12
 *          drivers/acpi/processor_idle.c), ACPI's policy between C2 and C3 (ACPI 6.6 §8.1.4).
 *
 * @param ladder  The ladder.
 * @param allowed A bit per depth the caller may enter.
 * @param depth   The depth asked, a depth of @p ladder.
 * @param status  PM1a status ORed with PM1b's, as read before the sleep.
 * @return The depth entered; @p depth itself when it has no check, or BM_STS is clear in @p status.
 */
uint32_t kernel_processor_sleep_depth_after_the_bus_master_check(const ProcessorSleepLadder_t *ladder, uint32_t allowed,
                                                                 uint32_t depth, uint32_t status);

/**
 * @brief Reads @p ladder's PM1 status registers, where the bus-master check reads BM_STS.
 *
 * @param host   The host the ports are read through: the machine's, or a simulated one in a test.
 * @param ladder The ladder whose status registers to read.
 * @return PM1a status ORed with PM1b's; 0 for a ladder that names none.
 */
uint32_t kernel_processor_sleep_read_bus_master_status(const struct AdvancedConfigurationAndPowerInterfaceHost *host,
                                                       const ProcessorSleepLadder_t *ladder);

/**
 * @brief Reads @p ladder's PM1 status registers before a depth with the bus-master check, and clears BM_STS where it
 *        is set, by writing it alone back to each, as ACPICA clears a status bit (v6.12 `acpi_write_bit_register`).
 *
 * @details Called with interrupts masked, by any processor: a clear another made only hides a request from the next
 *          check, and writing 1 again changes nothing.
 *
 * @param host   The host the ports are read and written through: the machine's, or a simulated one in a test.
 * @param ladder The ladder whose status registers to read.
 * @return PM1a status ORed with PM1b's, as read before the clear; 0 for a ladder that names none.
 */
uint32_t kernel_processor_sleep_check_bus_masters(const struct AdvancedConfigurationAndPowerInterfaceHost *host,
                                                  const ProcessorSleepLadder_t *ladder);

/**
 * @brief Sleeps the bus-master check demoted since the accounting began.
 * @return The count.
 */
uint32_t kernel_processor_sleep_bus_master_demotion_count(void);

/**
 * @brief Sleeps at an MWAIT depth whose monitor was not armed, entered by HLT instead, since the accounting began: an
 *        MWAIT that follows no MONITOR returns at once (Intel SDM vol. 2B, MWAIT), so that the sleep would spin.
 * @return The count, 0 unless a path enters a depth it did not arm the monitor for.
 */
uint32_t kernel_processor_sleep_unarmed_wait_count(void);

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
 * @brief Asks for a depth of the adopted ladder, lowered to the deepest available below it and counted when it is not
 *        available.
 *
 * @details Asking is not getting: a request past the available depths is lowered rather than passed through to raise
 *          a fault, to land somewhere unintended, or to stop the timer that wakes the core. What a depth costs in
 *          wake latency is NOT in CPUID: it lives in ACPI `_CST`, which the ladder carries and a tickless sleep
 *          reads as the lead of a depth it has not learned yet (kernel/power/tickless.h); choosing a depth against a
 *          deadline stays a decision of the caller that knows it (#299), never a derivation.
 *
 * @param depth The depth; @ref PROCESSOR_SLEEP_DEPTH_C1 is the floor, a depth past
 *              @ref PROCESSOR_SLEEP_DEPTH_DEEPEST asks for the deepest.
 * @return true when the request was granted exactly as asked.
 */
bool kernel_processor_sleep_request_depth(uint32_t depth);

/**
 * @brief The depth a sleep until an interrupt enters: the one @ref kernel_processor_sleep_request_depth last granted
 *        or @ref kernel_processor_sleep_refuse_depth last lowered to, once validated, the deepest validated below it
 *        until then (@ref kernel_processor_sleep_depth_awaiting_probe), and the active one while a probe is open.
 * @return That depth.
 */
uint32_t kernel_processor_sleep_depth(void);

/**
 * @brief Times a requested depth was lowered to the available ones.
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
 *          being handled just before it. MWAIT is given the active depth's hint where that depth is an MWAIT one,
 *          C1's otherwise: a port read watches no line. The interrupt flag is given back as it was found.
 *
 * @param watched  Address to watch; typically a driver's write index.
 * @param expected Value that means "nothing new yet".
 * @return How the processor was put to sleep, if at all.
 */
ProcessorSleepMode_t processor_sleep_until_write(const volatile uint32_t *watched, uint32_t expected);

/**
 * @brief Sleeps until the next interrupt, at the depth last granted.
 *
 * For an idle loop with nothing to watch, at the depth @ref kernel_processor_sleep_depth gives. An MWAIT depth arms
 * the monitor on a line nothing writes, so only an interrupt ends the wait, as with HLT; a halt is counted as one,
 * and a port depth reads its port. MWAIT and a port read may also end for reasons of their own with no interrupt
 * at all (ACPI 6.6 §8.1.3), and such a wake is counted as unattributed rather than credited to a source that did
 * not cause it.
 *
 * @details A depth with the bus-master check reads BM_STS first, interrupts masked, outside a probe: set, the sleep
 *          enters the depth below that @ref kernel_processor_sleep_depth_after_the_bus_master_check gives over the
 *          validated depths, and the outcome names it.
 *
 *          It may be entered with interrupts masked, and that is how a caller that arms a wake-up and then sleeps on
 *          it loses none: an interrupt that became pending since the caller masked them, its own timer's included,
 *          ends the sleep and is handled before the call returns. HLT and MWAIT come in the step that enables
 *          interrupts (STI then the instruction, Intel SDM vol. 3A, §7.8.1); a port is read with interrupts masked,
 *          a pending interrupt ending the state, and they are enabled right after, as Linux enters `io_idle` (v6.12
 *          `acpi_idle_do_entry`, drivers/acpi/processor_idle.c). The interrupt flag is given back as it was found.
 *
 * @return How the processor was put to sleep, and whether an interrupt was taken before the call returned.
 */
ProcessorSleepOutcome_t processor_sleep_until_interrupt(void);

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
 * @return The count, every mode together.
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

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)
/**
 * @brief Adopts @p ladder as an initialisation would, every depth available and C1 active, so that a debug kernel's
 *        tests give the module depths no QEMU model has; @ref kernel_processor_sleep_initialize adopts this
 *        processor's own again.
 * @details A tickless sleep keeps what it learned per depth, so a test that adopts a depth this processor's ladder
 *          does not have gives that depth's estimate back as it found it
 *          (kernel_tickless_debug_exchange_depth_estimate): the processor's own depth there would inherit it.
 * @param ladder The ladder.
 */
void kernel_processor_sleep_debug_adopt_ladder(const ProcessorSleepLadder_t *ladder);
#endif

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_PROCESSOR_SLEEP_H */
