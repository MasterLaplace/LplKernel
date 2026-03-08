/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** exception
*/

#ifndef EXCEPTION_H_
#define EXCEPTION_H_

/**
 * @brief Register dedicated handlers for critical CPU exceptions.
 *
 * Current bring-up installs explicit handlers for:
 *   - #DB (vector 1)
 *   - #BP (vector 3)
 *   - #UD (vector 6)
 *   - #DF (vector 8)
 *   - #GP (vector 13)
 *   - #PF (vector 14)
 */
extern void interrupt_exception_initialize(void);

#endif /* !EXCEPTION_H_ */
