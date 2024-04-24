/*
** LAPLACE PROJECT, 2024
** Laplace [LplKernel]
** Author: MasterLaplace
** Created: 2023-10-13
** File description:
** hello_world
*/

#ifdef __cplusplus
    #include <utility>
    #include <type_traits>
#endif

#include <stdint.h>
#include <stddef.h>

extern inline uint8_t inportb(int portnum)
{
    uint8_t data = 0;
    __asm__ __volatile__ ("inb %%dx, %%al" : "=a" (data) : "d" (portnum));
    return data;
}

extern inline void outportb(int portnum, uint8_t data)
{
    __asm__ __volatile__ ("outb %%al, %%dx" :: "a" (data),"d" (portnum));
}

#ifdef KERNEL_DEBUG
#define PORT 0xe9 /* QEMU debug port */
#else
#define PORT 0x3f8 /* COM1 */

static int init_serial()
{
    outportb(PORT + 1, 0x00);    // Disable all interrupts
    outportb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outportb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outportb(PORT + 1, 0x00);    //                  (hi byte)
    outportb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outportb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outportb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    outportb(PORT + 4, 0x1E);    // Set in loopback mode, test the serial chip
    outportb(PORT + 0, 0xAE);    // Send a test byte

    // Check that we received the same test byte we sent
    if(inportb(PORT + 0) != 0xAE)
        return 1;

    // If serial is not faulty set it in normal operation mode:
    // not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled
    outportb(PORT + 4, 0x0F);
    return 0;
}
#endif

void log_to_serial(char *string)
{
    if (string == NULL)
        return;

#ifndef KERNEL_DEBUG
    if (init_serial() != 0)
        return;
#endif

    for (; *string; string++)
        outportb(PORT, *string);
}

void digit_to_serial(uint64_t digit, uint8_t base)
{
    if (digit == 0)
    {
        outportb(PORT, '0');
        return;
    }

    uint8_t buffer[20];
    uint8_t i = 0;

    while (digit != 0)
    {
        buffer[i++] = (digit % base) + '0';
        digit /= base;
    }

    for (i--; i >= 0; i--)
        outportb(PORT, buffer[i]);
}

int main(void)
{
    log_to_serial("Hello, World!\n");
    digit_to_serial(1234567890, 10);
    return 0;
}
