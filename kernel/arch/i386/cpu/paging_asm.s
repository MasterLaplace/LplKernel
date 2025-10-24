.section .text
.global paging_get_cr3
.global paging_load_cr3
.global paging_invlpg
.global paging_flush_tlb

/**
 * @brief Get current page directory physical address from CR3
 *
 * C prototype: uint32_t paging_get_cr3(void);
 *
 * @return Physical address of current page directory
 */
.type paging_get_cr3, @function
paging_get_cr3:
    movl %cr3, %eax
    ret

/**
 * @brief Load a new page directory into CR3
 *
 * C prototype: void paging_load_cr3(uint32_t phys_addr);
 *
 * @param phys_addr Physical address of page directory (must be 4KB aligned)
 */
.type paging_load_cr3, @function
paging_load_cr3:
    movl 4(%esp), %eax      # Get physical address from stack
    movl %eax, %cr3         # Load into CR3 (flushes TLB)
    ret

/**
 * @brief Invalidate a single TLB entry
 *
 * C prototype: void paging_invlpg(uint32_t virt_addr);
 *
 * @param virt_addr Virtual address to invalidate in TLB
 */
.type paging_invlpg, @function
paging_invlpg:
    movl 4(%esp), %eax      # Get virtual address
    invlpg (%eax)           # Invalidate TLB entry for this address
    ret

/**
 * @brief Flush entire TLB by reloading CR3
 *
 * C prototype: void paging_flush_tlb(void);
 */
.type paging_flush_tlb, @function
paging_flush_tlb:
    movl %cr3, %eax         # Read current CR3
    movl %eax, %cr3         # Write it back (flushes TLB)
    ret
