/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2025 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file asmutils.h
 * @brief Thin wrappers over the x86 instructions C cannot express.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-05-17
 **************************************************************************/

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
 * @brief Write a 32-bit double word to an I/O port.
 *
 * @param port The I/O port address.
 * @param dword_value The 32-bit value to write.
 */
extern void asmutils_output_dword(uint16_t port, uint32_t dword_value);

/**
 * @brief Read a 32-bit double word from an I/O port.
 *
 * @param port The I/O port address.
 * @return The 32-bit value read from the port.
 */
extern uint32_t asmutils_input_dword(uint16_t port);

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
 * @brief Saves EFLAGS, then disables maskable interrupts.
 *
 * @details
 * The pair with @ref asmutils_restore_flags brackets a short critical section that
 * may itself be entered with interrupts already off: restoring the saved flags puts
 * IF back the way the caller found it, where a plain STI would re-enable interrupts
 * inside a caller that had disabled them on purpose.
 *
 * @return The EFLAGS value before CLI, to hand back to @ref asmutils_restore_flags.
 */
extern uint32_t asmutils_save_flags_and_disable_interrupts(void);

/**
 * @brief Restores EFLAGS saved by @ref asmutils_save_flags_and_disable_interrupts.
 *
 * @param flags The value that call returned.
 */
extern void asmutils_restore_flags(uint32_t flags);

/**
 * @brief Tells the core it is in a spin-wait.
 *
 * @details
 * PAUSE yields the pipeline to a sibling thread and stops the core from flooding
 * the memory bus with speculative reads of the location it is watching, which is
 * the energy argument for it. The call and return around it cost nothing that
 * matters: the loop it sits in is waiting anyway.
 */
extern void asmutils_pause(void);

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
 * @brief Read the CR0 control register.
 *
 * Bit 16 (WP, Write Protect) is the one that decides whether a read-only page
 * table entry means anything to ring 0 code: with WP clear, a supervisor write
 * to a read-only page succeeds and the R/W bit only ever constrains ring 3.
 * boot.S sets it together with PG, and the section protection reports it rather
 * than assuming it, because a protection that silently does nothing is worse
 * than no protection at all.
 *
 * @return The value of the CR0 register.
 */
extern uint32_t asmutils_read_control_register_0(void);

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
 *
 * @note %esi is the scratch pointer for the four output stores and is saved around
 *       them: it is callee-saved in the System V i386 ABI, and a caller keeping a loop
 *       variable there would otherwise see it overwritten by the leaf just read.
 */
extern void asmutils_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *out_eax, uint32_t *out_ebx, uint32_t *out_ecx,
                           uint32_t *out_edx);

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

/**
 * @brief Reads the full 64-bit timestamp counter.
 *
 * The whole counter and not its low word: an idle node sleeps for seconds at a
 * stretch, and at a gigahertz the low 32 bits wrap every four. This one is for
 * accounting that spans a whole boot; @ref asmutils_read_timestamp_counter_low is
 * for short durations.
 *
 * @return Cycles since reset.
 */
extern uint64_t asmutils_read_timestamp_counter(void);

/**
 * @brief Reads the low 32 bits of the timestamp counter.
 *
 * @details
 * What the allocators' worst-case timers need: they subtract two readings a few
 * hundred cycles apart, and unsigned subtraction stays correct across a wrap of the
 * low word as long as the interval is shorter than one.
 *
 * @return The low word of the cycle count.
 */
extern uint32_t asmutils_read_timestamp_counter_low(void);

/**
 * @brief Arms a watch on the cache line containing @p address.
 *
 * The processor remembers the line the address falls in, and a subsequent MWAIT sleeps
 * until anything writes it. That is what makes the pair better than HLT for a node whose
 * wake-up comes from a device's DMA rather than from an interrupt: no IRQ is needed and
 * no interrupt latency is paid.
 *
 * Pairs with @ref asmutils_monitor_wait. Between the two, the caller must re-check
 * the condition it is waiting on: if the write happened in that window the monitor
 * is already discarded, and the MWAIT would sleep for a wake-up that has been and
 * gone. That check is not optional and is the whole discipline of the pair.
 *
 * @param address    Address to watch; only its cache line matters.
 * @param extensions Currently zero on every processor that implements this.
 * @param hints      Currently zero.
 */
extern void asmutils_monitor(const void *address, uint32_t extensions, uint32_t hints);

/**
 * @brief Sleeps until the armed cache line is written.
 *
 * @param hints      Target C-state, encoded per Intel SDM Vol. 3B.
 * @param extensions Bit 0 makes an unmasked interrupt a break event as well, which is what
 *                   keeps a sleeping core answerable to a timer it also armed.
 */
extern void asmutils_monitor_wait(uint32_t hints, uint32_t extensions);

/**
 * @brief Stores a zero at @p target, publishing first the address to resume at if it faults.
 *
 * @details
 * The resume address is the instruction right after the store, written to
 * @p resume_address before the store executes. A page fault handler that recognises
 * the fault as expected sets the interrupted EIP to it, which steps over the store
 * instead of returning to it and faulting forever.
 *
 * @note Nothing is pushed between the entry and the store, so resuming there returns
 *       to the caller normally.
 *
 * @param target         Address to store to.
 * @param resume_address Receives the address to resume at.
 */
extern void asmutils_store_zero_with_resume_address(volatile uint8_t *target, volatile uint32_t *resume_address);

#endif /* KERNEL_LIB_ASMUTILS_H */
