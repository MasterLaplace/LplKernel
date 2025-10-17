#ifndef SERIAL_H_
#define SERIAL_H_

#include <kernel/lib/asmutils.h>
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

typedef struct __attribute__((packed)) {
    uint16_t port;
    uint16_t initialized;
    uint32_t speed;
} Serial_t;

////////////////////////////////////////////////////////////
// Public functions of the serial module API
////////////////////////////////////////////////////////////

extern void serial_initialize(Serial_t *serial, const COM_PORT port, const uint32_t speed);

extern void serial_write_char(Serial_t *serial, char c);

extern void serial_write_int(Serial_t *serial, int32_t i);

extern void serial_write_hex8(Serial_t *serial, uint8_t i);

extern void serial_write_hex16(Serial_t *serial, uint16_t i);

extern void serial_write_hex32(Serial_t *serial, uint32_t i);

extern void serial_write_hex64(Serial_t *serial, uint64_t i);

extern void serial_write_binary8(Serial_t *serial, uint8_t i);

extern void serial_write_string(Serial_t *serial, const char *data);

extern uint8_t serial_read_char(Serial_t *serial);

#endif /* !SERIAL_H_ */
