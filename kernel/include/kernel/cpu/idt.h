/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file idt.h
 * @brief Interrupt Descriptor Table layouts and loading.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-05
 **************************************************************************/

#ifndef KERNEL_CPU_INTERRUPT_DESCRIPTOR_TABLE_H
#define KERNEL_CPU_INTERRUPT_DESCRIPTOR_TABLE_H

#include <kernel/cpu/gdt.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>

#include <stddef.h>
#include <stdint.h>

/** Flags byte for a present, DPL=0, 32-bit interrupt gate (P=1, DPL=0, Type=0xE). */
#define IDT_KERNEL_INTERRUPT_GATE 0x8E
/** Flags byte for a present, DPL=3, 32-bit interrupt gate (used for syscall via int 0x80). */
#define IDT_USER_INTERRUPT_GATE 0xEE

typedef struct __attribute__((packed)) {
    uint8_t gate_type : 4; /**< 0x5 task, 0x6/0x7 16-bit interrupt/trap, 0xE/0xF 32-bit interrupt/trap gate. */
    uint8_t reserved  : 1; /**< Reserved, set to 0 */
    uint8_t descriptor_privilege_level : 2; /**< Descriptor privilege level (0 = highest, 3 = lowest) */
    uint8_t present                    : 1; /**< Segment present in memory */
} InterruptDescriptorTableTypeAttributes_t;

/** IDTR (IDT Register) structure for LIDT instruction in 32-bit mode. */
typedef struct __attribute__((packed)) {
    uint16_t size;   /**< Size of IDT - 1 (max 65535 bytes) */
    uint32_t offset; /**< Linear address of the first IDT entry */
} InterruptDescriptorTableRegisterFlat_t;

/** IDTR (IDT Register) structure for LIDT instruction in 64-bit mode. */
typedef struct __attribute__((packed)) {
    uint16_t size;   /**< Size of IDT - 1 (max 65535 bytes) */
    uint64_t offset; /**< Linear address of the first IDT entry */
} InterruptDescriptorTableRegisterLongMode_t;

typedef struct __attribute__((packed)) {
    uint16_t isr_low;                                         /**< offset bits 0..15 */
    uint16_t selector;                                        /**< a code segment selector in GDT or LDT */
    uint8_t reserved;                                         /**< unused, set to 0 */
    InterruptDescriptorTableTypeAttributes_t type_attributes; /**< gate type, dpl, and p fields */
    uint16_t isr_high;                                        /**< offset bits 16..31 */
} InterruptDescriptorTableFlatEntry_t;

typedef struct __attribute__((packed)) {
    uint16_t isr_low;              /**< offset bits 0..15 */
    uint16_t selector;             /**< a code segment selector in GDT or LDT */
    uint8_t interrupt_stack_table; /**< bits 0..2 holds Interrupt Stack Table offset, rest of bits zero. */
    InterruptDescriptorTableTypeAttributes_t type_attributes; /**< gate type, dpl, and p fields */
    uint16_t isr_mid;                                         /**< offset bits 16..31 */
    uint16_t isr_high;                                        /**< offset bits 32..63 */
    uint32_t reserved;                                        /**< unused, set to 0 */
} InterruptDescriptorTableLongModeEntry_t;

typedef struct __attribute__((aligned(0x10))) {
    InterruptDescriptorTableFlatEntry_t entries[256];
} InterruptDescriptorTableFlat_t;

typedef struct __attribute__((aligned(0x10))) {
    InterruptDescriptorTableLongModeEntry_t entries[256];
} InterruptDescriptorTableLongMode_t;

/** Alias for the most commonly used layout (Flat 32-bit protected mode). */
typedef InterruptDescriptorTableRegisterFlat_t InterruptDescriptorTableRegister_t;
typedef InterruptDescriptorTableFlatEntry_t InterruptDescriptorTableEntry_t;
typedef InterruptDescriptorTableFlat_t InterruptDescriptorTable_t;

/**
 * @brief Initialize a flat 32-bit IDT with exception and PIC IRQ handlers.
 *
 * Installs ISR stubs 0-31 and 32-47 as kernel interrupt gates
 * (DPL=0, selector=kernel code, flags=IDT_KERNEL_INTERRUPT_GATE).
 * Remaining entries are left zeroed.
 *
 * @param idt Pointer to the IDT structure to initialize.
 */
extern void interrupt_descriptor_table_initialize(InterruptDescriptorTable_t *idt);

/**
 * @brief Load and activate the IDT using LIDT.
 *
 * Constructs the IDTR (size = sizeof(IDT) - 1, base = &idt) and calls
 * the assembly stub. Interrupts are NOT enabled here — call sti() explicitly
 * after PIC initialization.
 *
 * @param idt Pointer to the initialized IDT structure.
 */
extern void interrupt_descriptor_table_load(InterruptDescriptorTable_t *idt);

#endif /* KERNEL_CPU_INTERRUPT_DESCRIPTOR_TABLE_H */
