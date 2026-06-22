/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** PCI (Peripheral Component Interconnect) configuration space access
*/

#ifndef KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_H
#define KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_H

#include <stdint.h>

/* Configuration mechanism #1 I/O ports. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_ADDRESS_PORT 0xCF8u
#define PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_DATA_PORT    0xCFCu

/* Common configuration-space register offsets (type 0/1 headers). */
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_VENDOR_ID      0x00u
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_DEVICE_ID      0x02u
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND        0x04u
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_STATUS         0x06u
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_REVISION_ID    0x08u
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_PROG_INTERFACE 0x09u
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_SUBCLASS       0x0Au
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_CLASS_CODE     0x0Bu
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_HEADER_TYPE    0x0Eu
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_BASE_ADDRESS_0 0x10u
#define PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_SECONDARY_BUS  0x19u

/* A missing device or function reads back this vendor id. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_INVALID_VENDOR_ID 0xFFFFu

/* Header type: bit 7 marks a multi-function device, low 7 bits select layout. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_HEADER_TYPE_MULTIFUNCTION_MASK 0x80u
#define PERIPHERAL_COMPONENT_INTERCONNECT_HEADER_TYPE_LAYOUT_MASK        0x7Fu

/**
 * @brief Read a 32-bit configuration register (mechanism #1).
 * @param bus PCI bus number.
 * @param device Device number (0-31).
 * @param function Function number (0-7).
 * @param offset Byte offset into config space (low 2 bits ignored).
 * @return The 32-bit register value.
 */
extern uint32_t peripheral_component_interconnect_config_read_dword(uint8_t bus, uint8_t device, uint8_t function,
                                                                    uint8_t offset);

/**
 * @brief Read a 16-bit configuration register.
 */
extern uint16_t peripheral_component_interconnect_config_read_word(uint8_t bus, uint8_t device, uint8_t function,
                                                                   uint8_t offset);

/**
 * @brief Read an 8-bit configuration register.
 */
extern uint8_t peripheral_component_interconnect_config_read_byte(uint8_t bus, uint8_t device, uint8_t function,
                                                                  uint8_t offset);

/**
 * @brief Write a 32-bit configuration register (mechanism #1).
 */
extern void peripheral_component_interconnect_config_write_dword(uint8_t bus, uint8_t device, uint8_t function,
                                                                 uint8_t offset, uint32_t value);

/**
 * @brief Write a 16-bit configuration register (read-modify-write of the dword).
 */
extern void peripheral_component_interconnect_config_write_word(uint8_t bus, uint8_t device, uint8_t function,
                                                                uint8_t offset, uint16_t value);

/**
 * @brief Write an 8-bit configuration register (read-modify-write of the dword).
 */
extern void peripheral_component_interconnect_config_write_byte(uint8_t bus, uint8_t device, uint8_t function,
                                                                uint8_t offset, uint8_t value);

/** @brief Maximum number of devices recorded by a single enumeration pass. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_MAX_DEVICES 64u

/**
 * @brief One enumerated PCI function (identity + classification).
 */
typedef struct PeripheralComponentInterconnectDevice {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_interface;
    uint8_t revision_id;
    uint8_t header_type;
} PeripheralComponentInterconnectDevice_t;

/**
 * @brief Brute-force enumerate every bus/device/function and record present
 *        devices (up to PERIPHERAL_COMPONENT_INTERCONNECT_MAX_DEVICES).
 *
 * Honours the multi-function header-type bit so single-function devices are not
 * probed on functions 1-7. Replaces the previous device table on each call.
 */
extern void peripheral_component_interconnect_scan(void);

/**
 * @brief Return the number of devices recorded by the last scan.
 */
extern uint32_t peripheral_component_interconnect_get_device_count(void);

/**
 * @brief Return a recorded device by index, or NULL when out of range.
 */
extern const PeripheralComponentInterconnectDevice_t *peripheral_component_interconnect_get_device(uint32_t index);

/** @brief Number of Base Address Registers in a type 0 configuration header. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_BASE_ADDRESS_REGISTER_COUNT 6u

/**
 * @brief A decoded Base Address Register (BAR).
 *
 * A 64-bit memory BAR consumes two consecutive slots; when @ref is_64bit is set
 * the caller should skip the following index. @ref size is probed from the low
 * 32 bits, which covers any region up to 4 GiB.
 */
typedef struct PeripheralComponentInterconnectBaseAddressRegister {
    uint8_t index;        /**< Slot index (0-5).                          */
    uint8_t is_io;        /**< Non-zero for I/O space, zero for MMIO.      */
    uint8_t is_64bit;     /**< Non-zero for a 64-bit MMIO BAR.            */
    uint8_t prefetchable; /**< Non-zero when the MMIO region is prefetchable. */
    uint64_t base;        /**< Decoded base address.                      */
    uint64_t size;        /**< Region size in bytes (0 when unimplemented). */
} PeripheralComponentInterconnectBaseAddressRegister_t;

/**
 * @brief Decode one Base Address Register of a device, probing its size.
 *
 * Size probing temporarily writes all-ones to the BAR and restores it; do not
 * call concurrently with active use of the device.
 *
 * @param bus PCI bus number.
 * @param device Device number (0-31).
 * @param function Function number (0-7).
 * @param index BAR index (0-5).
 * @param out_bar Destination for the decoded BAR.
 * @return Non-zero when the BAR is implemented (size != 0); zero otherwise.
 */
extern uint8_t peripheral_component_interconnect_read_base_address_register(
    uint8_t bus, uint8_t device, uint8_t function, uint8_t index,
    PeripheralComponentInterconnectBaseAddressRegister_t *out_bar);

#endif /* KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_H */
