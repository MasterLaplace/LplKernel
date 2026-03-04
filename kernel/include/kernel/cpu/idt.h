/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** idt
*/

#ifndef IDT_H_
#define IDT_H_

#include <stdint.h>

/// Flags byte for a present, DPL=0, 32-bit interrupt gate (P=1, DPL=0, Type=0xE)
#define IDT_KERNEL_INTERRUPT_GATE 0x8E
/// Flags byte for a present, DPL=3, 32-bit interrupt gate (used for syscall via int 0x80)
#define IDT_USER_INTERRUPT_GATE 0xEE

typedef struct __attribute__((packed)) {
    uint8_t gate_type : 4; // Gate type (0x5 = 32-bit Task Gate, 0x6 = 16-bit Interrupt Gate, 0x7 = 16-bit Trap Gate,
                           // 0xE = 32-bit Interrupt Gate, 0xF = 32-bit Trap Gate)
    uint8_t reserved                   : 1; // Reserved, set to 0
    uint8_t descriptor_privilege_level : 2; // Descriptor privilege level (0 = highest, 3 = lowest)
    uint8_t present                    : 1; // Segment present in memory
} InterruptDescriptorTableTypeAttributes_t;

/// IDTR (IDT Register) structure for LIDT instruction in 32-bit mode
typedef struct __attribute__((packed)) {
    uint16_t size;   // Size of IDT - 1 (max 65535 bytes)
    uint32_t offset; // Linear address of the first IDT entry
} InterruptDescriptorTableRegisterFlat_t;

/// IDTR (IDT Register) structure for LIDT instruction in 64-bit mode
typedef struct __attribute__((packed)) {
    uint16_t size;   // Size of IDT - 1 (max 65535 bytes)
    uint64_t offset; // Linear address of the first IDT entry
} InterruptDescriptorTableRegisterLongMode_t;

typedef struct __attribute__((packed)) {
    uint16_t isr_low;                                         // offset bits 0..15
    uint16_t selector;                                        // a code segment selector in GDT or LDT
    uint8_t reserved;                                         // unused, set to 0
    InterruptDescriptorTableTypeAttributes_t type_attributes; // gate type, dpl, and p fields
    uint16_t isr_high;                                        // offset bits 16..31
} InterruptDescriptorTableFlatEntry_t;

typedef struct __attribute__((packed)) {
    uint16_t isr_low;              // offset bits 0..15
    uint16_t selector;             // a code segment selector in GDT or LDT
    uint8_t interrupt_stack_table; // bits 0..2 holds Interrupt Stack Table offset, rest of bits zero.
    InterruptDescriptorTableTypeAttributes_t type_attributes; // gate type, dpl, and p fields
    uint16_t isr_mid;                                         /// offset bits 16..31
    uint16_t isr_high;                                        // offset bits 32..63
    uint32_t reserved;                                        // unused, set to 0
} InterruptDescriptorTableLongModeEntry_t;

typedef struct __attribute__((aligned(0x10))) {
    InterruptDescriptorTableFlatEntry_t entries[256];
} InterruptDescriptorTableFlat_t;

typedef struct __attribute__((aligned(0x10))) {
    InterruptDescriptorTableLongModeEntry_t entries[256];
} InterruptDescriptorTableLongMode_t;

/// Alias for the most commonly used layout (Flat 32-bit protected mode)
typedef InterruptDescriptorTableRegisterFlat_t InterruptDescriptorTableRegister_t;
typedef InterruptDescriptorTableFlatEntry_t InterruptDescriptorTableEntry_t;
typedef InterruptDescriptorTableFlat_t InterruptDescriptorTable_t;

////////////////////////////////////////////////////////////
// Public API functions of the IDT module
////////////////////////////////////////////////////////////

/**
 * @brief Initialize a flat 32-bit IDT with all 32 CPU exception handlers.
 *
 * Installs ISR stubs 0–31 as kernel interrupt gates (DPL=0, selector=0x08,
 * flags=IDT_KERNEL_INTERRUPT_GATE). Remaining entries are left zeroed.
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

#endif /* !IDT_H_ */
