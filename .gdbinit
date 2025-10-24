# GDB initialization file for LplKernel debugging

# Connect to QEMU GDB stub (will be done by launch.json, but useful if running GDB manually)
# target remote localhost:1234

# Load kernel symbols
# file kernel/lpl.kernel

# Set Intel disassembly syntax (more readable for x86)
set disassembly-flavor intel

# Enable pretty printing
set print pretty on
set print array on
set print array-indexes on

# Show source code when stopping
set listsize 20

# Don't ask for confirmation
set confirm off

# Enable history
set history save on
set history size 10000
set history filename ~/.gdb_history

# Disable pagination (useful for automated debugging)
set pagination off

# Display layout (useful for TUI mode)
# layout split
# layout regs

# Helper commands for kernel debugging
define hook-stop
    # This runs every time execution stops
    info registers
    x/i $pc
end

# Custom command to dump page directory
define dump_pd
    if $argc == 0
        set $pd = boot_page_directory
    else
        set $pd = $arg0
    end
    printf "Page Directory at 0x%08x:\n", $pd
    set $i = 0
    while $i < 1024
        set $pde = *((unsigned int*)$pd + $i)
        if $pde & 0x1
            printf "  PDE[%d] = 0x%08x (P:%d RW:%d U:%d PS:%d)\n", \
                $i, $pde, ($pde & 0x1), ($pde & 0x2) >> 1, \
                ($pde & 0x4) >> 2, ($pde & 0x80) >> 7
        end
        set $i = $i + 1
    end
end
document dump_pd
Dump page directory entries
Usage: dump_pd [address]
If no address is given, uses boot_page_directory
end

# Custom command to dump GDT
define dump_gdt
    printf "GDT Entries:\n"
    printf "Note: Use 'p global_descriptor_table' to see full structure\n"
end
document dump_gdt
Dump GDT entries
end

# Custom command to show current CPU mode
define show_mode
    set $cr0 = $cr0
    set $cr4 = $cr4
    printf "CPU Mode:\n"
    printf "  CR0.PE (Protected Mode): %d\n", ($cr0 & 0x1)
    printf "  CR0.PG (Paging): %d\n", ($cr0 & 0x80000000) >> 31
    printf "  CR4.PAE (PAE): %d\n", ($cr4 & 0x20) >> 5
    printf "  EFLAGS.IF (Interrupts): %d\n", ($eflags & 0x200) >> 9
end
document show_mode
Show current CPU mode and important control register bits
end

# Breakpoint on triple fault (useful for debugging crashes)
# catch signal SIGSEGV

# Print helpful message
printf "\n"
printf "========================================\n"
printf "  LplKernel GDB Debugging Session\n"
printf "========================================\n"
printf "Useful commands:\n"
printf "  dump_pd [addr]  - Dump page directory\n"
printf "  dump_gdt        - Dump GDT info\n"
printf "  show_mode       - Show CPU mode\n"
printf "  info registers  - Show all registers\n"
printf "  info threads    - Show all threads (CPUs)\n"
printf "  x/20i $pc       - Disassemble at PC\n"
printf "  layout split    - Enable TUI mode\n"
printf "========================================\n"
printf "\n"
