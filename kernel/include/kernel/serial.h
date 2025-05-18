#ifndef SERIAL_H_
#define SERIAL_H_

#include <kernel/asmutils.h>
#include <stdint.h>

#define BASE_SERIAL_SPEED 115200

/* Standard PC serial port addresses. */
typedef enum COM_PORT {
    COM1 = 0x3F8,
    COM2 = 0x2F8,
    COM3 = 0x3E8,
    COM4 = 0x2E8,
    COM5 = 0x5F8,
    COM6 = 0x4F8,
    COM7 = 0x5E8,
    COM8 = 0x4E8
} COM_PORT;

typedef struct serial_s {
    uint16_t port;
    uint16_t initialized;
    uint32_t speed;
} serial_t;

////////////////////////////////////////////////////////////
// Public functions of the serial module API
////////////////////////////////////////////////////////////

extern void serial_initialize(serial_t *serial, const COM_PORT port, const uint32_t speed);

extern void serial_write_char(serial_t *serial, char c);

extern void serial_write_string(serial_t *serial, const char *data);

extern uint8_t serial_read_char(serial_t *serial);

#endif /* !SERIAL_H_ */
