/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** irq
*/

#ifndef IRQ_H_
#define IRQ_H_

#include <stdint.h>

/**
 * @brief Initialize IRQ runtime for current bring-up stage.
 *
 * This routine remaps the PIC, masks all IRQ lines, installs the IRQ0
 * timer handler, unmasks IRQ0, then enables interrupts globally.
 */
extern void interrupt_request_initialize(void);

/**
 * @brief Return IRQ0 tick count since IRQ initialization.
 */
extern uint32_t interrupt_request_get_tick_count(void);

#endif /* !IRQ_H_ */
