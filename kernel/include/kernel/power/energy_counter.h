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
 * @file energy_counter.h
 * @brief The package energy counter of Intel's and AMD's RAPL: what the processor has, and the joules between two
 *        reads.
 *
 * RAPL counts the energy a package used in a 32-bit register, in units its unit register gives. No CPUID bit tells it
 * on Intel, where Linux matches a table of models (v6.12 drivers/powercap/intel_rapl_common.c, `rapl_ids`); AMD and
 * Hygon tell it by CPUID 0x80000007 EDX bit 14 (v6.12 arch/x86/kernel/cpu/amd.c, `X86_FEATURE_RAPL`). A hypervisor may
 * fault the registers or answer them with 0, as KVM and TCG do: the counter is taken only once its unit register reads
 * without a fault, gives a unit, and the counter moves.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-13
 **************************************************************************/

#ifndef KERNEL_POWER_ENERGY_COUNTER_H
#define KERNEL_POWER_ENERGY_COUNTER_H

#include <kernel/cpu/cpu_identity.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name RAPL's registers (Linux v6.12 arch/x86/include/asm/msr-index.h)
 * @{
 */
#define KERNEL_ENERGY_COUNTER_INTEL_UNIT_MSR        0x606u      /**< MSR_RAPL_POWER_UNIT. */
#define KERNEL_ENERGY_COUNTER_INTEL_POWER_LIMIT_MSR 0x610u      /**< MSR_PKG_POWER_LIMIT. */
#define KERNEL_ENERGY_COUNTER_INTEL_PACKAGE_MSR     0x611u      /**< MSR_PKG_ENERGY_STATUS. */
#define KERNEL_ENERGY_COUNTER_INTEL_POWER_INFO_MSR  0x614u      /**< MSR_PKG_POWER_INFO. */
#define KERNEL_ENERGY_COUNTER_AMD_UNIT_MSR          0xC0010299u /**< MSR_AMD_RAPL_POWER_UNIT. */
#define KERNEL_ENERGY_COUNTER_AMD_PACKAGE_MSR       0xC001029Bu /**< MSR_AMD_PKG_ENERGY_STATUS. */
/** @} */

/**
 * The wait across which a counter that counts moves: the package counter updates about every millisecond (Intel SDM
 * vol. 3, 325384-093US, §17.10.3), and a package that idles still uses energy.
 */
#define KERNEL_ENERGY_COUNTER_SETTLE_MICROSECONDS 5000u

/**
 * @enum KernelEnergyCounterBackend_t
 * @brief Whose registers a processor's energy counter is read from, and in which units.
 */
typedef enum {
    KERNEL_ENERGY_COUNTER_BACKEND_NONE = 0,       /**< No RAPL the kernel knows of. */
    KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE = 1, /**< Intel's registers, energy in 1/2^ESU J, power in 1/2^PU W. */
    KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM = 2, /**< Intel's registers, energy in 2^ESU uJ, power in 2^PU mW. */
    KERNEL_ENERGY_COUNTER_BACKEND_AMD = 3,        /**< AMD's registers, energy in 1/2^ESU J. */
} KernelEnergyCounterBackend_t;

/**
 * @enum KernelEnergyCounterState_t
 * @brief Whether the counter is read, and why not.
 */
typedef enum {
    KERNEL_ENERGY_COUNTER_PRESENT = 0,      /**< It is. */
    KERNEL_ENERGY_COUNTER_NOT_DETECTED = 1, /**< No detection ran yet. */
    KERNEL_ENERGY_COUNTER_NOT_LISTED = 2,   /**< The processor is no RAPL one the kernel knows of. */
    KERNEL_ENERGY_COUNTER_FAULTED = 3,      /**< Its unit register or its counter faulted. */
    KERNEL_ENERGY_COUNTER_NO_UNIT = 4,      /**< Its unit register gives no energy unit, as KVM's reads 0. */
    KERNEL_ENERGY_COUNTER_FROZEN = 5,       /**< Its counter did not move across the settling wait. */
    KERNEL_ENERGY_COUNTER_UNTIMED = 6,      /**< No calibrated clock could time the settling wait. */
} KernelEnergyCounterState_t;

/**
 * @brief What a detection found of a processor's energy counter.
 */
typedef struct KernelEnergyCounter {
    uint32_t backend;                         /**< A KernelEnergyCounterBackend_t. */
    uint32_t state;                           /**< A KernelEnergyCounterState_t. */
    uint32_t package_msr;                     /**< The package counter's MSR; 0 without a backend. */
    uint32_t energy_unit_exponent;            /**< ESU, bits 12:8 of the unit register; 0 before it is read. */
    uint32_t power_unit_exponent;             /**< PU, bits 3:0 of the unit register. */
    uint32_t thermal_design_power_milliwatts; /**< Intel's power info, bits 14:0, when known. */
    uint32_t power_limit_milliwatts;          /**< Intel's power limit 1, bits 14:0, when known. */
    bool thermal_design_power_known;          /**< Whether the power info read, on Intel. */
    bool power_limit_known;                   /**< Whether the power limit read, on Intel. */
} KernelEnergyCounter_t;

/**
 * @brief Reads one MSR for a detection, and says whether the processor gave it.
 * @param context What the detection was given.
 * @param msr     The register.
 * @param out_value Receives its value when given.
 * @return true when the processor gave it.
 */
typedef bool (*KernelEnergyCounterReader_t)(void *context, uint32_t msr, uint64_t *out_value);

/**
 * @brief Waits for a detection, and says whether it could time the wait.
 * @param context      What the detection was given.
 * @param microseconds How long.
 * @return true once that long has passed; false when no clock could time it.
 */
typedef bool (*KernelEnergyCounterWait_t)(void *context, uint32_t microseconds);

/**
 * @brief The backend of a processor: Intel family 6 at one of the 55 models of Linux v6.12's `rapl_ids`, four of them
 *        (0x37, 0x4A, 0x4C, 0x5A, Silvermont and Airmont) in Atom units; AMD or Hygon whose CPUID 0x80000007 EDX sets
 *        bit 14.
 * @param identity           The processor's vendor and its displayed family and model.
 * @param extended_power_edx CPUID 0x80000007 EDX, 0 where the leaf does not exist.
 * @return A KernelEnergyCounterBackend_t.
 */
uint32_t kernel_energy_counter_backend_of(const CpuIdentity_t *identity, uint32_t extended_power_edx);

/**
 * @brief The nanojoules of @p counts units of energy: counts * 10^9 / 2^ESU (Intel SDM vol. 3, §17.10.1), or
 *        counts * 1000 * 2^ESU on an Atom (v6.12 intel_rapl_common.c, `rapl_check_unit_atom`).
 * @param backend              A KernelEnergyCounterBackend_t; none gives 0.
 * @param energy_unit_exponent ESU, at most 31.
 * @param counts               Units, as the 32-bit counter counts them.
 * @return The nanojoules, rounded down; the largest value for a product past 64 bits.
 */
uint64_t kernel_energy_counter_nanojoules(uint32_t backend, uint32_t energy_unit_exponent, uint32_t counts);

/**
 * @brief The units the 32-bit counter counted from @p before to @p after, one wrap allowed: two reads must be closer
 *        than a wrap, 2^32 units, "around 60 secs when power consumption is high" (Intel SDM vol. 3, §17.10.3), 44
 *        minutes at 61 uJ and 100 W.
 * @param before The earlier read.
 * @param after  The later one.
 * @return after - before, modulo 2^32.
 */
uint32_t kernel_energy_counter_counts_between(uint32_t before, uint32_t after);

/**
 * @brief The milliwatts of @p raw units of power: raw * 1000 / 2^PU, or raw * 2^PU on an Atom.
 * @param backend             A KernelEnergyCounterBackend_t; none gives 0.
 * @param power_unit_exponent PU, at most 15.
 * @param raw                 A power field, 15 bits.
 * @return The milliwatts, rounded down; the largest value for a result past 32 bits.
 */
uint32_t kernel_energy_counter_milliwatts(uint32_t backend, uint32_t power_unit_exponent, uint32_t raw);

/**
 * @brief Detects the counter of @p backend through @p read and @p wait: its unit register must read and give an energy
 *        unit, and its counter must read twice, KERNEL_ENERGY_COUNTER_SETTLE_MICROSECONDS apart, and move; then, on
 *        Intel, the power info and the power limit are read, each known only where it reads.
 * @param backend A KernelEnergyCounterBackend_t.
 * @param read    Reads one register.
 * @param wait    Times the settling wait.
 * @param context What @p read and @p wait receive.
 * @param out     Receives what was found, written whole.
 */
void kernel_energy_counter_detect(uint32_t backend, KernelEnergyCounterReader_t read, KernelEnergyCounterWait_t wait,
                                  void *context, KernelEnergyCounter_t *out);

/**
 * @brief Detects this processor's counter, through the fault-tolerant RDMSR and the calibrated TSC.
 *
 * @details Called once the local timer is calibrated, on the bootstrap processor; the energy is the package's.
 */
void kernel_energy_counter_initialize(void);

/**
 * @brief What the last detection found.
 * @return The finding; KERNEL_ENERGY_COUNTER_NOT_DETECTED before any.
 */
const KernelEnergyCounter_t *kernel_energy_counter(void);

/**
 * @brief Reads the package counter.
 * @param out_counts Receives its 32 bits.
 * @return false when the counter is not present, or its read faulted.
 */
bool kernel_energy_counter_sample(uint32_t *out_counts);

/**
 * @brief The nanojoules the package used between two samples, one wrap allowed.
 * @param before The earlier sample.
 * @param after  The later one.
 * @return The nanojoules; 0 when the counter is not present, which only a caller whose samples failed can meet.
 */
uint64_t kernel_energy_counter_nanojoules_between(uint32_t before, uint32_t after);

/**
 * @brief The name the telemetry gives a backend.
 * @param backend A KernelEnergyCounterBackend_t.
 * @return "none", "intel_core", "intel_atom" or "amd"; "unknown" otherwise.
 */
const char *kernel_energy_counter_backend_name(uint32_t backend);

/**
 * @brief The name the telemetry gives a state.
 * @param state A KernelEnergyCounterState_t.
 * @return "present", "not_detected", "not_listed", "faulted", "no_unit", "frozen" or "untimed"; "unknown" otherwise.
 */
const char *kernel_energy_counter_state_name(uint32_t state);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_ENERGY_COUNTER_H */
