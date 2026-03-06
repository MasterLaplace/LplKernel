/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** smoke_test
*/

#ifndef SMOKE_TEST_H_
#define SMOKE_TEST_H_

#include <kernel/drivers/serial.h>

#define KERNEL_SMOKE_TEST_ENABLE_PMM_ALLOCATE_FREE  1u
#define KERNEL_SMOKE_TEST_ENABLE_DIVISION_ERROR     0u
#define KERNEL_SMOKE_TEST_ENABLE_GRAPHICS_DEMO      1u
#define KERNEL_SMOKE_TEST_ENABLE_IRQ_RUNTIME_STATUS 1u
#define KERNEL_SMOKE_TEST_ENABLE_RTC_SNAPSHOT       1u

extern void kernel_smoke_test_run_physical_memory_manager_allocate_free(Serial_t *serial_port);

extern void kernel_smoke_test_run_division_error(void);

extern void kernel_smoke_test_run_graphics_demo(Serial_t *serial_port);

extern void kernel_smoke_test_run_interrupt_request_runtime_status(Serial_t *serial_port);

extern void kernel_smoke_test_run_realtime_clock_snapshot(Serial_t *serial_port);

#endif /* !SMOKE_TEST_H_ */
