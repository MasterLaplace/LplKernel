#ifndef KERNEL_LIB_ASMUTILS_H
#define KERNEL_LIB_ASMUTILS_H

#include <stdint.h>

/**
 * @brief Write a byte to an I/O port.
 *
 * @param port The I/O port address.
 * @param byte_value The byte value to write.
 */
extern void asmutils_output_byte(uint16_t port, uint8_t byte_value);

/**
 * @brief Read a byte from an I/O port.
 *
 * @param port The I/O port address.
 * @return The byte value read from the port.
 */
extern uint8_t asmutils_input_byte(uint16_t port);

/**
 * @brief Enable CPU interrupts.
 *
 * @details
 * Executes the x86 STI (Set Interrupt Flag) instruction to enable maskable interrupts.
 * This is a privileged operation that must be called from ring 0.
 */
extern void asmutils_enable_interrupts(void);

/**
 * @brief Disable CPU interrupts.
 *
 * @details
 * Executes the x86 CLI (Clear Interrupt Flag) instruction to disable maskable interrupts.
 * This is a privileged operation that must be called from ring 0.
 */
extern void asmutils_disable_interrupts(void);

/**
 * @brief Halt the CPU.
 *
 * @details
 * Executes the x86 HLT (Halt) instruction, which stops instruction execution
 * until an interrupt is received. Typically used in idle loops or for power saving.
 */
extern void asmutils_halt(void);

/**
 * @brief Execute a CPU no-operation instruction.
 *
 * @details
 * Executes the x86 NOP (No Operation) instruction. Used for timing, alignment,
 * or placeholder purposes.
 */
extern void asmutils_no_operation(void);

/**
 * @brief Get the current CPU stack pointer.
 *
 * @return The value of the ESP (stack pointer) register.
 */
extern uint32_t asmutils_get_current_stack_pointer(void);

/**
 * @brief Invalidate the TLB by reloading CR3.
 *
 * @details
 * Reloads the CR3 register with its current value, forcing the CPU to flush
 * all Translation Lookaside Buffer (TLB) entries. Used after modifying page tables.
 */
extern void asmutils_invalidate_translation_lookaside_buffer(void);

/**
 * @brief Get the linear address that caused a page fault.
 *
 * @return The value of the CR2 register, which contains the linear address
 *         that triggered the most recent page fault exception.
 */
extern uint32_t asmutils_get_page_fault_linear_address(void);

/**
 * @brief Execute the CPUID instruction.
 *
 * @param leaf The CPUID leaf (input EAX).
 * @param subleaf The CPUID subleaf (input ECX).
 * @param out_eax Pointer to store the output EAX register value. May be NULL.
 * @param out_ebx Pointer to store the output EBX register value. May be NULL.
 * @param out_ecx Pointer to store the output ECX register value. May be NULL.
 * @param out_edx Pointer to store the output EDX register value. May be NULL.
 *
 * @details
 * Executes the x86 CPUID instruction with the given leaf and subleaf parameters.
 * Returns CPU capability and feature information. Output pointers are checked
 * for NULL before writing.
 */
extern void asmutils_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *out_eax, uint32_t *out_ebx,
                           uint32_t *out_ecx, uint32_t *out_edx);

/**
 * @brief Read a Model-Specific Register (MSR).
 *
 * @param msr_id The MSR identifier (ECX value for RDMSR instruction).
 * @return The 64-bit MSR value.
 *
 * @details
 * Executes the x86 RDMSR (Read MSR) instruction. This is a privileged operation
 * that must be called from ring 0. If the MSR is invalid or not accessible,
 * a #GP (General Protection fault) exception may occur.
 */
extern uint64_t asmutils_read_model_specific_register(uint32_t msr_id);

/**
 * @brief Write a Model-Specific Register (MSR).
 *
 * @param msr_id The MSR identifier (ECX value for WRMSR instruction).
 * @param value The 64-bit value to write to the MSR.
 *
 * @details
 * Executes the x86 WRMSR (Write MSR) instruction. This is a privileged operation
 * that must be called from ring 0. If the MSR is invalid, read-only, or not accessible,
 * a #GP (General Protection fault) exception may occur.
 */
extern void asmutils_write_model_specific_register(uint32_t msr_id, uint64_t value);

#endif /* KERNEL_LIB_ASMUTILS_H */
