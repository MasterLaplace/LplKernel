#define LAPLACE_KERNEL_PANIC
#include <kernel/config.h>

#include <kernel/multiboot_info.h>
#include <kernel/tty.h>
#include <kernel/serial.h>
// #include <kernel/gdt.h>

static const char WELCOME_MESSAGE[] = ""
"/==+--  _                                         ---+\n"
"| \\|   | |                  _                        |\n"
"+  |   | |     __ _  ____  | |    __ _  ____  ___    |\n"
"   |   | |    / _` ||  _ \\ | |   / _` |/ ___\\/ _ \\   |\n"
"   |   | |___| (_| || |_| || |__| (_| | (___|  __/   |\\\n"
"   |   \\_____|\\__,_|| ___/ \\____|\\__,_|\\____/\\___|   ||\n"
"   |                | |_                             | \\\n"
"   +---             |___|                          --+==+\n\n";

static Serial_t com1;
// static MultibootInfo_t *mbi = NULL;
// uint32_t multiboot_magic;
// MultibootInfo_t *multiboot_info_ptr;

__attribute__ ((constructor)) void kernel_initialize(uint32_t magic, MultibootInfo_t *mbi)
// __attribute__ ((constructor)) void kernel_initialize(void)
{
    // mbi = (MultibootInfo_t *)((uintptr_t)info + 0xC0000000);
    // mbi = info;
    // gdt_initialize();
    // mbi = (MultibootInfo_t *)(multiboot_info_ptr);
    serial_initialize(&com1, COM1, 9600);
    serial_write_string(&com1, "["KERNEL_SYSTEM_STRING"]: serial port initialisation successful.\n");
    terminal_initialize();

    // serial_write_hex32(&com1, magic);
    // serial_write_hex32(&com1, (uint32_t)mbi);

    // // serial_write_hex32(&com1, (int32_t)mbi);


    // // Par exemple, tu peux afficher les flags ou voir la mémoire :
    // if (mbi->flags & (1 << 0)) {
    //     uint32_t mem_lower = mbi->mem_lower;
    //     uint32_t mem_upper = mbi->mem_upper;
    //     serial_write_string(&com1, "Memory lower: ");
    //     serial_write_hex32(&com1, mem_lower);
    //     serial_write_string(&com1, " KB\n");
    //     serial_write_string(&com1, "Memory upper: ");
    //     serial_write_hex32(&com1, mem_upper);
    //     serial_write_string(&com1, " KB\n");
    //     // Utilise les infos...
    // }
}

void kernel_main(void)
{
    // if (magic != MULTIBOOT_MAGIC) {
        // printf("Erreur multiboot: magic=0x%08x\n", magic);
    //     return -1;
    // }


    // printf("Multiboot OK: mem_lower=%u KB, mem_upper=%u KB\n", mbi->mem_lower, mbi->mem_upper);

    // if (multiboot_magic != MULTIBOOT_MAGIC) {
    //     /* Gérer l'erreur */
    //     for (;;) ;
    // }

    // /* Accès à la mémoire selon multiboot_info_ptr */
    // if (multiboot_info_ptr->flags & MULTIBOOT_FLAG_MEMINFO) {
    //     uint32_t lower = multiboot_info_ptr->mem_lower;
    //     uint32_t upper = multiboot_info_ptr->mem_upper;
    //     /* ... utilisation ... */
    // }

    // /* Suite de votre kernel ... */

    terminal_write_string(WELCOME_MESSAGE);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_write_string("["KERNEL_SYSTEM_STRING"]: loading ...\n\n");
    // serial_write_hex32(&com1, mbi->mem_lower);
    // serial_write_string(&com1, " KB RAM\n");
    // serial_write_hex32(&com1, mbi->mem_upper);
    // serial_write_string(&com1, " KB RAM\n");
    serial_write_string(&com1, "["KERNEL_SYSTEM_STRING"]: loaded successfully.\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_write_string(KERNEL_CONFIG_STRING);

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    uint8_t c = 0u;
    for (; (uint32_t)c != 27u;)
    {
        c = serial_read_char(&com1);
        terminal_putchar(c);
    }
}

__attribute__ ((destructor)) void kernel_cleanup(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write_string("\n["KERNEL_SYSTEM_STRING"]: exiting ... panic!\n");
}
