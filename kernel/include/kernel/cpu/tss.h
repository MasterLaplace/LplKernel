/*
** EPITECH PROJECT, 2025
** LplKernel [WSL : Ubuntu-24.04]
** File description:
** tss
*/

#ifndef TSS_H_
#define TSS_H_

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t link;
    uint16_t reserved0;
    uint32_t estack_pointer0;
    uint16_t segment_selector0;
    uint16_t reserved1;
    uint32_t estack_pointer1;
    uint16_t segment_selector1;
    uint16_t reserved2;
    uint32_t estack_pointer2;
    uint16_t segment_selector2;
    uint16_t reserved3;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflages;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint16_t es;
    uint16_t reserved4;
    uint16_t cs;
    uint16_t reserved5;
    uint16_t ss;
    uint16_t reserved6;
    uint16_t ds;
    uint16_t reserved7;
    uint16_t fs;
    uint16_t reserved8;
    uint16_t gs;
    uint16_t reserved9;
    uint16_t ldtr;
    uint16_t reserved10;
    uint16_t reserve11;
    uint16_t io_map_base;
    uint32_t shadow_stack_pointer;
} TaskStateSegment_t;

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t interrupt_stack_table1;
    uint64_t interrupt_stack_table2;
    uint64_t interrupt_stack_table3;
    uint64_t interrupt_stack_table4;
    uint64_t interrupt_stack_table5;
    uint64_t interrupt_stack_table6;
    uint64_t interrupt_stack_table7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} TaskStateSegmentLongMode_t;

////////////////////////////////////////////////////////////
// External assembly functions
////////////////////////////////////////////////////////////

/**
 * @brief Load TSS using LTR instruction (implemented in tss.s)
 * @param tss_selector Selector of the TSS in the GDT
 */
extern void task_state_segment_load(const uint16_t tss_selector);

////////////////////////////////////////////////////////////
// Public functions of the TSS module API
////////////////////////////////////////////////////////////

/**
 * @brief Initialize basic TSS fields (SS0, ESP0, I/O map base)
 *
 * @param tss Pointer to the TSS structure to initialize
 * @param kernel_ss_selector Kernel Data Segment selector for ring 0 (SS0)
 */
extern void task_state_segment_initialize(TaskStateSegment_t *tss, uint16_t kernel_ss_selector);

#endif /* !TSS_H_ */
