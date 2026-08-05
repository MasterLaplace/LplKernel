/**
 * @file inference_budget.h
 * @brief Where a headless profile's spare capacity goes.
 *
 * A server image renders nothing. Rather than leaving that capacity implicit, it
 * is declared, bounded and accounted here, so 'the demon may think' never becomes
 * 'the demon missed the tick'. The real-time guard already distinguishes bounded
 * from unbounded work; inference is simply another claimant under the same rule.
 *
 * The unit is TOKENS, and the choice matters. `engine::InferenceBudget` counts turns
 * for the same reason: everything downstream of an answer is deterministic and
 * replayable, so the answer must be too — and a budget measured on a wall clock
 * would make the reply depend on how fast the machine was that day. A budget spent
 * in tokens is a budget two targets agree about.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_AI_INFERENCE_BUDGET_H
#define KERNEL_AI_INFERENCE_BUDGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opens a budget of @p tokens.
 *
 * @param tokens Tokens the demon may produce before it must conclude.
 */
void kernel_inference_budget_open(uint32_t tokens);

/**
 * @brief Spends one token.
 * @return true when there was one to spend.
 */
bool kernel_inference_budget_claim(void);

/**
 * @brief Tokens left.
 * @return The remainder.
 */
uint32_t kernel_inference_budget_remaining(void);

/**
 * @brief Tokens spent since the budget was opened.
 * @return The count.
 */
uint32_t kernel_inference_budget_spent(void);

/**
 * @brief Claims refused because the budget was empty.
 *
 * Counted rather than merely refused: a demon that is routinely cut short is a
 * budget that is too small, and that is only visible as a number.
 *
 * @return The refusal count.
 */
uint32_t kernel_inference_budget_denied(void);

/**
 * @brief Is the budget in its final tenth?
 *
 * The signal to wrap up rather than to stop. A reply cut off mid-sentence at
 * exhaustion is worse than a shorter one that concludes, and the caller can only
 * make that choice if it is told before the money runs out.
 *
 * @return true when a tenth or less remains.
 */
bool kernel_inference_budget_concluding(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_AI_INFERENCE_BUDGET_H */
