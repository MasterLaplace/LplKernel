/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2025 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file serial.h
 * @brief 16550 UART serial port driver.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-05-17
 **************************************************************************/

#ifndef KERNEL_DRIVERS_SERIAL_H
#define KERNEL_DRIVERS_SERIAL_H

#include <kernel/lib/asmutils.h>
#include <stdint.h>

#define BASE_SERIAL_SPEED 115200

/** Standard PC serial port addresses. */
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

extern Serial_t com1;

/**
 * @brief Initializes a serial port.
 *
 * @param serial Serial port descriptor.
 * @param port COM port address.
 * @param speed Baud rate (bits per second).
 */
extern void serial_initialize(Serial_t *serial, const COM_PORT port, const uint32_t speed);

/**
 * @brief Writes a single character to the serial port.
 * @param serial Output port.
 * @param c Character to write.
 */
extern void serial_write_char(Serial_t *serial, char c);

/**
 * @brief Writes a signed value in base ten.
 *
 * @details The full range: INT32_MIN prints as -2147483648, and INT32_MAX prints as 2147483647.
 *
 * @param serial Output port.
 * @param i Value to write.
 */
extern void serial_write_int(Serial_t *serial, int32_t i);

/**
 * @brief Writes an unsigned value in base ten.
 *
 * @details The full range: a value of 2^31 or more has no int32_t spelling, and passing
 *          one through @ref serial_write_int prints it negative.
 *
 * @param serial Output port.
 * @param value Value to write.
 */
extern void serial_write_unsigned(Serial_t *serial, uint32_t value);

/**
 * @brief Writes a value in hexadecimal, with 0x prefix and leading zeros.
 * @param serial Output port.
 * @param i Value to write.
 */
extern void serial_write_hex8(Serial_t *serial, uint8_t i);

/**
 * @brief Writes a value in hexadecimal, with 0x prefix and leading zeros.
 * @param serial Output port.
 * @param i Value to write.
 */
extern void serial_write_hex16(Serial_t *serial, uint16_t i);

/**
 * @brief Writes a value in hexadecimal, with 0x prefix and leading zeros.
 * @param serial Output port.
 * @param i Value to write.
 */
extern void serial_write_hex32(Serial_t *serial, uint32_t i);

/**
 * @brief Writes a value in hexadecimal, with 0x prefix and leading zeros.
 * @param serial Output port.
 * @param i Value to write.
 */
extern void serial_write_hex64(Serial_t *serial, uint64_t i);

/**
 * @brief Writes a value in binary, with 0b prefix and leading zeros.
 * @param serial Output port.
 * @param i Value to write.
 */
extern void serial_write_binary8(Serial_t *serial, uint8_t i);

/**
 * @brief Writes a null-terminated string.
 * @param serial Output port.
 * @param data String to write.
 */
extern void serial_write_string(Serial_t *serial, const char *data);

/**
 * @brief Reads a single character from the serial port.
 * @param serial Output port.
 * @return The character read, or 0 if no data is available.
 */
extern uint8_t serial_read_char(Serial_t *serial);

/**
 * @brief Try to read one byte from serial without blocking.
 *
 * @param serial Serial port descriptor.
 * @param out_char Destination byte when data is available.
 * @return 1 when one byte was read, 0 when no data is pending or args invalid.
 */
extern uint8_t serial_try_read_char(Serial_t *serial, uint8_t *out_char);

#endif /* KERNEL_DRIVERS_SERIAL_H */
