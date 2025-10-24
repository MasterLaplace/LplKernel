/*
** EPITECH PROJECT, 2025
** LplKernel [WSL : Ubuntu]
** File description:
** gdt
*/

#ifndef GDT_H_
#define GDT_H_

#include <stdbool.h>
#include <stdint.h>

/// Access byte structure for GDT entries
typedef struct __attribute__((packed)) {
    uint8_t accessed                   : 1; // Segment accessed
    uint8_t read_write                 : 1; // Readable for code segments, writable for data segments
    uint8_t direction_conforming       : 1; // Direction bit for data segments, conforming bit for code segments
    uint8_t executable                 : 1; // 1 = code segment, 0 = data segment
    uint8_t descriptor_type            : 1; // 1 = code/data segment, 0 = system segment
    uint8_t descriptor_privilege_level : 2; // Descriptor privilege level (0 = highest, 3 = lowest)
    uint8_t present                    : 1; // Segment present in memory
} GlobalDescriptorTableAccessByte_t;

/// Flags structure for GDT entries
typedef struct __attribute__((packed)) {
    uint8_t limit_high  : 4; // bits 16..19 of segment limit
    uint8_t reserved    : 1; // Reserved for system use
    uint8_t long_mode   : 1; // 64-bit code segment (IA-32e mode only)
    uint8_t default_big : 1; // D/B (Default/Big) bit: 0 = 16-bit (default operand/stack size), 1 = 32-bit
    uint8_t granularity : 1; // Granularity: limit scaled by 4K when set
} GlobalDescriptorTableFlags_t;

/// Standard 32-bit GDT entry structure
typedef struct __attribute__((packed)) {
    uint16_t limit_low;                            // bits 0..15 of segment limit
    uint16_t base_low;                             // bits 0..15 of base
    uint8_t base_middle;                           // bits 16..23 of base
    GlobalDescriptorTableAccessByte_t access_byte; // access byte
    GlobalDescriptorTableFlags_t flags;            // flags (G, D/B, L, AVL) in high nibble and limit_high in low nibble
    uint8_t base_high;                             // bits 24..31 of base
} GlobalDescriptorTableEntry_t;

/// 64-bit GDT entry structure (for IA-32e mode)
typedef struct __attribute__((packed)) {
    GlobalDescriptorTableEntry_t base; // Standard 32-bit GDT entry
    uint32_t base_upper;               // bits 32..63 of base
    uint32_t reserved;                 // reserved, set to 0
} GlobalDescriptorTableLongModeEntry_t;

/// GDTR (GDT Register) structure for LGDT instruction in 32-bit mode
typedef struct __attribute__((packed)) {
    uint16_t limit; // Size of GDT - 1 (max 65535 bytes)
    uint32_t base;  // Linear address of the first GDT entry
} GlobalDescriptorTablePointer_t;

/// Small Kernel Setup (from OSDev wiki GDT_Tutorial "Small Kernel Setup" table)
/// This layout is designed for testing and matches the example with separated segments
/// Offsets: 0x0000 (null), 0x0008 (kcode), 0x0010 (kdata), 0x0018 (tss)
typedef struct __attribute__((packed)) {
    GlobalDescriptorTableEntry_t null_descriptor;          // 0x0000: Null Descriptor (required)
    GlobalDescriptorTableEntry_t kernel_mode_code_segment; // 0x0008: Kernel Mode Code Segment
    GlobalDescriptorTableEntry_t kernel_mode_data_segment; // 0x0010: Kernel Mode Data Segment
    GlobalDescriptorTableEntry_t task_state_segment;       // 0x0018: Task State Segment (TSS)
} GlobalDescriptorTableSmall_t;

/// Flat / Long Mode Setup - 32-bit Protected Mode (from OSDev wiki "Flat / Long Mode Setup" first table)
/// This is the recommended layout for modern kernels using a flat memory model with paging.
/// All segments have Base=0 and Limit=0xFFFFF with G=1 (covering full 4 GiB address space).
/// Offsets: 0x0000 (null), 0x0008 (kcode), 0x0010 (kdata), 0x0018 (ucode), 0x0020 (udata), 0x0028 (tss)
typedef struct __attribute__((packed)) {
    GlobalDescriptorTableEntry_t null_descriptor;          // 0x0000: Null Descriptor
    GlobalDescriptorTableEntry_t kernel_mode_code_segment; // 0x0008: Kernel Mode Code Segment (DPL=0)
    GlobalDescriptorTableEntry_t kernel_mode_data_segment; // 0x0010: Kernel Mode Data Segment (DPL=0)
    GlobalDescriptorTableEntry_t user_mode_code_segment;   // 0x0018: User Mode Code Segment (DPL=3)
    GlobalDescriptorTableEntry_t user_mode_data_segment;   // 0x0020: User Mode Data Segment (DPL=3)
    GlobalDescriptorTableEntry_t task_state_segment;       // 0x0028: Task State Segment (TSS)
} GlobalDescriptorTableFlat_t;

/// Flat / Long Mode Setup - 64-bit Long Mode (from OSDev wiki "Flat / Long Mode Setup" second table)
/// This layout is for IA-32e (x86-64) long mode. Base and Limit are ignored for code/data in long mode.
/// Note: TSS in 64-bit mode takes 16 bytes (two GDT entries), so the total is larger.
/// Offsets: 0x0000 (null), 0x0008 (kcode64), 0x0010 (kdata), 0x0018 (udata), 0x0020 (ucode64), 0x0028 (tss 16-byte)
typedef struct __attribute__((packed)) {
    GlobalDescriptorTableEntry_t null_descriptor;            // 0x0000: Null Descriptor
    GlobalDescriptorTableEntry_t kernel_mode_code_segment;   // 0x0008: Kernel Mode Code Segment (64-bit, L=1)
    GlobalDescriptorTableEntry_t kernel_mode_data_segment;   // 0x0010: Kernel Mode Data Segment
    GlobalDescriptorTableEntry_t user_mode_data_segment;     // 0x0018: User Mode Data Segment (DPL=3)
    GlobalDescriptorTableEntry_t user_mode_code_segment;     // 0x0020: User Mode Code Segment (64-bit, L=1, DPL=3)
    GlobalDescriptorTableLongModeEntry_t task_state_segment; // 0x0028: 64-bit TSS (16 bytes, two entries)
} GlobalDescriptorTableLongMode_t;

/// Alias for the most commonly used layout (Flat 32-bit protected mode)
typedef GlobalDescriptorTableFlat_t GlobalDescriptorTable_t;

////////////////////////////////////////////////////////////
// Public functions of the GDT module API
////////////////////////////////////////////////////////////

/**
 * @brief Initialize a flat 32-bit GDT with standard kernel/user segments
 * @param gdt Pointer to the GDT structure to initialize
 */
extern void global_descriptor_table_init(GlobalDescriptorTable_t *gdt);

/**
 * @brief Load and activate a GDT
 *
 * This function:
 *  1. Sets up the GDTR pointer with the GDT base address and limit
 *  2. Loads the GDT using LGDT instruction
 *  3. Reloads all segment registers (CS via far jump, DS/ES/FS/GS/SS)
 *
 * @param gdt Pointer to the initialized GDT structure
 */
extern void global_descriptor_table_load(GlobalDescriptorTable_t *gdt);

#endif /* GDT_H_ */
