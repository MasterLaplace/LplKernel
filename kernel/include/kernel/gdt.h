/*
** EPITECH PROJECT, 2025
** LplKernel [WSL : Ubuntu]
** File description:
** gdt
*/

#ifndef GDT_H_
#define GDT_H_

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

/// Pointer structure for the LGDT instruction
typedef struct __attribute__((packed)) {
    uint16_t limit; // Size of GDT - 1 (max 65536 bytes)
    uint32_t base;  // Address of the first GDT entry
} GlobalDescriptorTablePointer_t;

/// Global Descriptor Table structure with standard segments
typedef struct __attribute__((packed)) {
    GlobalDescriptorTablePointer_t pointer;                 // Pointer for LGDT instruction
    GlobalDescriptorTableEntry_t null_segment;              // Null segment (required)
    GlobalDescriptorTableEntry_t unused_segment;            // Unused segment
    GlobalDescriptorTableEntry_t code_segment;              // Code segment
    GlobalDescriptorTableEntry_t data_segment;              // Data segment
} GlobalDescriptorTable_t;

/// Global Descriptor Table structure with long mode segments (for IA-32e mode)
typedef struct __attribute__((packed)) {
    GlobalDescriptorTablePointer_t pointer;                     // Pointer for LGDT instruction
    GlobalDescriptorTableEntry_t null_segment;                  // Null segment (required)
    GlobalDescriptorTableEntry_t unused_segment;                // Unused segment
    GlobalDescriptorTableLongModeEntry_t code_segment;          // 64-bit code segment
    GlobalDescriptorTableLongModeEntry_t data_segment;          // 64-bit data segment
    GlobalDescriptorTableEntry_t code_segment_compatibility;    // 32-bit compatibility mode code segment
    GlobalDescriptorTableEntry_t data_segment_compatibility;    // 32-bit compatibility mode data segment
} GlobalDescriptorTableLongMode_t;

#endif /* GDT_H_ */
