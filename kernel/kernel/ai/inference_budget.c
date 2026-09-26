#include <kernel/ai/inference_budget.h>

static uint32_t inference_budget_granted = 0u;
static uint32_t inference_budget_remaining = 0u;
static uint32_t inference_budget_denied = 0u;

void kernel_inference_budget_open(uint32_t tokens)
{
    inference_budget_granted = tokens;
    inference_budget_remaining = tokens;
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
    return inference_budget_remaining * 10u <= inference_budget_granted;
}
