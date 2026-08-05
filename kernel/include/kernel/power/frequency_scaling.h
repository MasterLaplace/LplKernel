/**
 * @file frequency_scaling.h
 * @brief Trading clock for battery, deliberately.
 *
 * Wake-word detection needs a fraction of the processor. Scaling down when only that
 * is running is the single largest saving available, and it is safe precisely because
 * the satellite profile has no frame deadline to miss.
 *
 * Dynamic voltage and frequency scaling is quadratic in the voltage and linear in the
 * clock (LplKernel_Book §9.2.1), which is why it beats every other lever: halving the
 * clock and dropping the voltage with it cuts dynamic power by rather more than half.
 * The catch is that only the hardware can apply it, and most of the hardware this
 * kernel is developed on does not implement it at all.
 *
 * So this module separates the two halves and is honest about which one it can do.
 * The GOVERNOR — deciding what performance state the current load deserves — is pure
 * arithmetic and always runs. The APPLICATION — writing it to a model-specific
 * register — happens only where the processor advertises Enhanced SpeedStep, and is
 * reported as refused otherwise. A governor that silently did nothing would be a
 * power feature that shows up in every log and saves nothing.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_POWER_FREQUENCY_SCALING_H
#define KERNEL_POWER_FREQUENCY_SCALING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum PerformanceState_t
 * @brief What the governor decided the load deserves.
 *
 * Named after the ACPI P-states they map onto, coarsened to three because a kernel
 * with no ACPI `_PSS` table has no list of the intermediate ones and inventing them
 * would be inventing frequencies the silicon may not have.
 */
typedef enum
{
    PERFORMANCE_STATE_LOW = 0,  /**< Idle or wake-word only: the slowest state offered. */
    PERFORMANCE_STATE_MEDIUM,   /**< Streaming or playing: enough to meet the buffer cadence. */
    PERFORMANCE_STATE_HIGH,     /**< Everything else: no scaling. */
} PerformanceState_t;

/**
 * @brief Probes what the processor offers and zeroes the accounting.
 *
 * Enhanced SpeedStep is CPUID leaf 1, ECX bit 7. Probed and not assumed: writing
 * IA32_PERF_CTL on a processor that does not implement it raises a general
 * protection fault, and the machine this is developed on is exactly such a processor.
 */
void kernel_frequency_scaling_initialize(void);

/**
 * @brief Can this processor actually change its clock?
 * @return true when Enhanced SpeedStep is advertised.
 */
bool kernel_frequency_scaling_available(void);

/**
 * @brief The state a load deserves.
 *
 * Pure arithmetic, no hardware. Separated from applying it so the decision can be
 * tested, folded and reasoned about on a machine that cannot carry it out — which is
 * every machine in this project's build farm.
 *
 * @param busy_permille Share of the last interval spent awake, in thousandths.
 * @param deadline_bound True when something must be delivered on a cadence, which
 *                       forbids the lowest state whatever the load says.
 * @return The state.
 */
PerformanceState_t kernel_frequency_scaling_govern(uint32_t busy_permille, bool deadline_bound);

/**
 * @brief Requests a performance state.
 *
 * @param state What the governor decided.
 * @return true when the hardware accepted it; false when there is no hardware to
 *         accept it, which is recorded rather than treated as success.
 */
bool kernel_frequency_scaling_request(PerformanceState_t state);

/**
 * @brief The state most recently requested.
 * @return The state, @ref PERFORMANCE_STATE_HIGH before any request.
 */
PerformanceState_t kernel_frequency_scaling_current(void);

/**
 * @brief Requests the hardware carried out.
 * @return The count.
 */
uint32_t kernel_frequency_scaling_applied_count(void);

/**
 * @brief Requests refused because the processor cannot scale.
 *
 * The number that stops this module from looking like it is working. A profile that
 * reported a low performance state while every request was refused would be
 * reporting an intention.
 *
 * @return The count.
 */
uint32_t kernel_frequency_scaling_refused_count(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_FREQUENCY_SCALING_H */
