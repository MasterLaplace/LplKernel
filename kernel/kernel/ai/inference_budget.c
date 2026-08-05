/**
 * @file inference_budget.c
 * @brief Where a headless profile's spare capacity goes.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <kernel/ai/inference_budget.h>

static uint32_t inference_budget_granted = 0u;
static uint32_t inference_budget_remaining = 0u;
static uint32_t inference_budget_denied = 0u;

void kernel_inference_budget_open(uint32_t tokens)
{
    inference_budget_granted = tokens;
    inference_budget_remaining = tokens;
    /* The refusal count deliberately survives an open: it answers "is this budget
       the right size", which is a question about the run and not about one reply. */
}

bool kernel_inference_budget_claim(void)
{
    if (inference_budget_remaining == 0u)
    {
        ++inference_budget_denied;
        return false;
    }
    --inference_budget_remaining;
    return true;
}

uint32_t kernel_inference_budget_remaining(void) { return inference_budget_remaining; }

uint32_t kernel_inference_budget_spent(void) { return inference_budget_granted - inference_budget_remaining; }

uint32_t kernel_inference_budget_denied(void) { return inference_budget_denied; }

bool kernel_inference_budget_concluding(void)
{
    if (inference_budget_granted == 0u)
        return true;
    /* Multiplied rather than divided, so a budget of nine tokens still has a final
       tenth: nine divided by ten is zero, and the signal would never fire. */
    return inference_budget_remaining * 10u <= inference_budget_granted;
}
