/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** pic
*/

#ifndef PIC_H_
#define PIC_H_

#include <stdint.h>

#define PIC_VECTOR_OFFSET_MASTER 32u
#define PIC_VECTOR_OFFSET_SLAVE  40u

/**
 * @brief Initialize and remap legacy 8259 PIC to vectors 32-47.
 */
extern void programmable_interrupt_controller_initialize(void);

/**
 * @brief Mask a specific IRQ line.
 */
extern void programmable_interrupt_controller_set_mask(uint8_t irq_line);

/**
 * @brief Unmask a specific IRQ line.
 */
extern void programmable_interrupt_controller_clear_mask(uint8_t irq_line);

/**
 * @brief Send End-Of-Interrupt to the PIC(s) for a served IRQ.
 */
extern void programmable_interrupt_controller_send_end_of_interrupt(uint8_t irq_line);

/**
 * @brief Return whether an IRQ line is currently marked in-service by PIC.
 *
 * This is used to distinguish real IRQ7/IRQ15 from spurious interrupts.
 */
extern uint8_t programmable_interrupt_controller_is_in_service(uint8_t irq_line);

#endif /* !PIC_H_ */
