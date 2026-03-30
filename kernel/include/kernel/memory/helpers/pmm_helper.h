/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** pmm_helper
*/

#ifndef KERNEL_MEMORY_PMM_HELPER_H
#define KERNEL_MEMORY_PMM_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_pmm_strategy_info(Serial_t *serial);
extern void write_pmm_pass_1_info(Serial_t *serial);
extern void write_pmm_pass_2_start_info(Serial_t *serial);
extern void write_pmm_ready_info(Serial_t *serial);
extern void write_pmm_buddy_info(Serial_t *serial);
extern void write_pmm_guard_info(Serial_t *serial);

#endif /* KERNEL_MEMORY_PMM_HELPER_H */
