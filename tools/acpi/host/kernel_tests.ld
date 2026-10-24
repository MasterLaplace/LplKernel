/* The section KERNEL_TEST puts each test in, bounded by the symbols the kernel's runner walks, added
   to the host linker's own script. */
SECTIONS
{
    .kernel_tests : {
        _kernel_tests_start = .;
        KEEP(*(.kernel_tests))
        _kernel_tests_end = .;
    }
}
INSERT AFTER .data;
