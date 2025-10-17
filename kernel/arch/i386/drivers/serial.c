#include <kernel/drivers/serial.h>

////////////////////////////////////////////////////////////
// Private functions of the serial module
////////////////////////////////////////////////////////////

static inline int serial_can_write(Serial_t *serial)
{
    return inb(serial->port + 5) & 0x20;
}

static inline int serial_can_read(Serial_t *serial)
{
    return inb(serial->port + 5) & 0x01;
}

////////////////////////////////////////////////////////////
// Public functions of the terminal module API
////////////////////////////////////////////////////////////

void serial_initialize(Serial_t *serial, COM_PORT port, uint32_t speed)
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

void serial_write_char(Serial_t *serial, char c)
{
    while (!serial_can_write(serial));
    outb(serial->port, c);
}

void serial_write_int(Serial_t *serial, int32_t i)
{
    char buffer[12];
    int j = 0;

    if (i < 0)
    {
        serial_write_char(serial, '-');
        i = -i;
    }

    if (i == 0)
    {
        serial_write_char(serial, '0');
        return;
    }

    while (i > 0)
    {
        buffer[j++] = '0' + (i % 10);
        i /= 10;
    }

    while (--j >= 0)
        serial_write_char(serial, buffer[j]);
}

void serial_write_hex8(Serial_t *serial, uint8_t i)
{
    char hex[5];
    hex[0] = '0';
    hex[1] = 'x';

    for (uint8_t j = 0; j < 2; ++j)
    {
        uint8_t nibble = (i >> ((1 - j) * 4)) & 0xF;
        hex[2 + j] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }

    hex[4] = '\0';
    serial_write_string(serial, hex);
}

void serial_write_hex16(Serial_t *serial, uint16_t i)
{
    char hex[7];
    hex[0] = '0';
    hex[1] = 'x';

    for (uint8_t j = 0; j < 4; ++j)
    {
        uint16_t nibble = (i >> ((3 - j) * 4)) & 0xF;
        hex[2 + j] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }

    hex[6] = '\0';
    serial_write_string(serial, hex);
}

void serial_write_hex32(Serial_t *serial, uint32_t i)
{
    char hex[11];
    hex[0] = '0';
    hex[1] = 'x';

    for (uint8_t j = 0; j < 8; ++j)
    {
        uint32_t nibble = (i >> ((7 - j) * 4)) & 0xF;
        hex[2 + j] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }

    hex[10] = '\0';
    serial_write_string(serial, hex);
}

void serial_write_hex64(Serial_t *serial, uint64_t i)
{
    char hex[19];
    hex[0] = '0';
    hex[1] = 'x';

    for (uint8_t j = 0; j < 16; ++j)
    {
        uint32_t nibble = (i >> ((15 - j) * 4)) & 0xF;
        hex[2 + j] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }

    hex[18] = '\0';
    serial_write_string(serial, hex);
}

void serial_write_binary8(Serial_t *serial, uint8_t i)
{
    char bin[11];
    bin[0] = '0';
    bin[1] = 'b';

    for (uint8_t j = 0; j < 8; ++j)
    {
        bin[2 + j] = (i & (1 << (7 - j))) ? '1' : '0';
    }

    bin[10] = '\0';
    serial_write_string(serial, bin);
}

void serial_write_string(Serial_t *serial, const char *data)
{
    for (; data && *data; ++data)
    {
        serial_write_char(serial, *data);
    }
}

uint8_t serial_read_char(Serial_t *serial)
{
    while (!serial_can_read(serial));
    return inb(serial->port);
}
