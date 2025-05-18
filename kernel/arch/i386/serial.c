#include <kernel/serial.h>

////////////////////////////////////////////////////////////
// Private functions of the serial module
////////////////////////////////////////////////////////////

static inline int serial_can_write(serial_t *serial)
{
    return inb(serial->port + 5) & 0x20;
}

static inline int serial_can_read(serial_t *serial)
{
    return inb(serial->port + 5) & 0x01;
}

////////////////////////////////////////////////////////////
// Public functions of the terminal module API
////////////////////////////////////////////////////////////

void serial_initialize(serial_t *serial, COM_PORT port, uint32_t speed)
{
    serial->port = port;
    serial->speed = speed;
    serial->initialized = 0;

    outb(port + 1, 0x00);
    outb(port + 3, 0x80);
    outb(port, (BASE_SERIAL_SPEED / speed) % 256);
    outb(port + 1, (BASE_SERIAL_SPEED / speed) % 256);
    outb(port + 3, 0x03);
    outb(port + 2, 0xC7);
    outb(port + 4, 0x0B);
    outb(port + 4, 0x1E);
    outb(port, 0xAE);

    if (inb(port) != 0xAE)
        return;

    outb(port + 4, 0x0F);
    serial->initialized = 1;
}

void serial_write_char(serial_t *serial, char c)
{
    while (!serial_can_write(serial));
    outb(serial->port, c);
}

void serial_write_string(serial_t *serial, const char *data)
{
    for (; data && *data; ++data)
    {
        serial_write_char(serial, *data);
    }
}

uint8_t serial_read_char(serial_t *serial)
{
    while (!serial_can_read(serial));
    return inb(serial->port);
}
