#ifndef ASMUTILS_H_
#define ASMUTILS_H_

#include <stdint.h>

extern void outb(short port, unsigned char toSend);

extern unsigned char inb(short port);

extern void cpu_enable_interrupts(void);

extern void cpu_disable_interrupts(void);

extern void cpu_halt(void);

extern void cpu_no_operation(void);

extern uint32_t cpu_get_current_stack_pointer(void);

extern void cpu_reload_page_directory(void);

extern uint32_t cpu_get_page_fault_linear_address(void);

#endif /* !ASMUTILS_H_ */
