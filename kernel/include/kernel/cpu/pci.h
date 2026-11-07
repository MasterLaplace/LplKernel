/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file pci.h
 * @brief PCI (Peripheral Component Interconnect) configuration space access.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-06-23
 **************************************************************************/

#ifndef KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_H
#define KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @name Configuration mechanism #1 I/O ports
 * @{
 */
#define PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_ADDRESS_PORT 0xCF8u
#define PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_DATA_PORT    0xCFCu
/** @} */

/**
 * @name Common configuration-space register offsets (type 0/1 headers)
 * @{
 */
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
/** @} */

/** A missing device or function reads back this vendor id. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_INVALID_VENDOR_ID 0xFFFFu

/** Header type: bit 7 marks a multi-function device, low 7 bits select layout. */
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

/**
 * @brief Reads 1, 2 or 4 bytes of a function's configuration space in one access of that width, which
 *        touches no other register.
 *
 * @param bus      PCI bus number.
 * @param device   Device number (0-31).
 * @param function Function number (0-7).
 * @param offset   Byte offset into config space, which @p width divides.
 * @param width    1, 2 or 4.
 * @return The bytes read, all ones for an absent function.
 */
extern uint32_t peripheral_component_interconnect_config_read(uint8_t bus, uint8_t device, uint8_t function,
                                                              uint8_t offset, uint8_t width);

/**
 * @brief Writes 1, 2 or 4 bytes of a function's configuration space in one access of that width, so
 *        that the bytes around them, such as status bits a write of 1 clears, are left as they are.
 *
 * @param bus      PCI bus number.
 * @param device   Device number (0-31).
 * @param function Function number (0-7).
 * @param offset   Byte offset into config space, which @p width divides.
 * @param width    1, 2 or 4.
 * @param value    The bytes to write, in the low @p width bytes.
 */
extern void peripheral_component_interconnect_config_write(uint8_t bus, uint8_t device, uint8_t function,
                                                           uint8_t offset, uint8_t width, uint32_t value);

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

/**
 * @brief Finds the first enumerated device of a given class and subclass.
 *
 * A driver looks for a KIND of device, not for a slot, and every one of them would
 * otherwise open the same loop over @ref peripheral_component_interconnect_get_device
 * and compare the same two fields. Written once here so the audio codec, the network
 * card and whatever comes next ask the question the same way.
 *
 * @param class_code Class byte, e.g. 0x04 for a multimedia device.
 * @param subclass   Subclass byte, e.g. 0x03 for High Definition Audio.
 * @return The device, or NULL when the bus carries none.
 */
extern const PeripheralComponentInterconnectDevice_t *
peripheral_component_interconnect_find_by_class(uint8_t class_code, uint8_t subclass);

/**
 * @name Bits of the command register
 * @{
 */
#define PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_MEMORY_SPACE (1u << 1) /**< The device answers its memory window. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_BUS_MASTER                                                           \
    (1u << 2) /**< The device may master the bus, as DMA does. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_INTERRUPT_DISABLE                                                    \
    (1u << 10) /**< The device asserts no pin interrupt (INTx); MSI and MSI-X are not affected. */
/** @} */

/**
 * @brief Lets a device answer its memory window and master the bus.
 *
 * @note Bus mastering is the one that is easy to forget and hard to diagnose: without it every
 *       register reads correctly and the device never touches the memory it was given.
 *
 * @param device The device.
 */
extern void peripheral_component_interconnect_enable_memory_and_bus_mastering(
    const PeripheralComponentInterconnectDevice_t *device);

/**
 * @brief Stops a device from asserting its pin interrupt (INTx).
 *
 * @details For a device its driver polls. Left on, the device asserts its line once it completes
 *          work, nothing ever de-asserts it, and a driver that takes the same line for itself
 *          never gets out of its handler.
 *
 * @param device The device.
 */
extern void
peripheral_component_interconnect_disable_pin_interrupt(const PeripheralComponentInterconnectDevice_t *device);

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

/**
 * @brief Can this kernel map the memory window a decoded Base Address Register describes?
 *
 * @details This kernel maps a 32-bit physical space, so a window placed above 4 GiB is out of its
 *          reach even behind a 64-bit register pair. A driver refuses such a window rather than cut
 *          its base to the low 32 bits and map whatever lies there, uncached and writable. A window
 *          that starts below 4 GiB and ends past it is refused by kernel_vmm_map_device_window.
 *
 * @param bar A decoded Base Address Register.
 * @return true for a memory window whose base lies below 4 GiB; false for an I/O window, or for a
 *         memory window above 4 GiB.
 */
extern bool peripheral_component_interconnect_memory_window_is_reachable(
    const PeripheralComponentInterconnectBaseAddressRegister_t *bar);

/**
 * @name The configuration space through ECAM, the memory-mapped windows the MCFG gives
 * @{
 */
#define PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_CONFIGURATION_SIZE                                                   \
    0x1000u /**< Bytes of one function's configuration space through ECAM. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_LEGACY_CONFIGURATION_SIZE                                                    \
    0x100u /**< Bytes of a function's configuration space configuration mechanism #1 reaches. */

/**
 * @brief What peripheral_component_interconnect_express_judge_window says of an ECAM window.
 */
typedef enum {
    PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_USABLE = 0, /**< Accesses may go through it once declared. */
    PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_PAST_REACH, /**< Its buses end past 4 GiB. */
    PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_OVER_RAM,   /**< Its buses cover usable RAM. */
} PeripheralComponentInterconnectExpressWindow_t;

/**
 * @brief The ECAM windows of the boot's MCFG, as peripheral_component_interconnect_express_initialize sorted them.
 */
typedef struct {
    uint32_t declared;   /**< Windows declared uncached, the only ones accesses go through. */
    uint32_t past_reach; /**< Windows refused because their buses end past 4 GiB. */
    uint32_t over_ram;   /**< Windows refused because their buses cover usable RAM. */
    uint32_t no_room;    /**< Usable windows the virtual memory manager had no room to declare, refused too. */
} PeripheralComponentInterconnectExpressWindowCounts_t;

/**
 * @brief Says whether accesses may go through an ECAM window, from its range alone.
 *
 * @details The window's configuration space runs from @p base_address plus @p start_bus MiB up to @p base_address
 *          plus (@p end_bus + 1) MiB. It must end below 4 GiB, which this kernel's 32-bit physical space reaches
 * (#569), the sum taken without wrapping whatever base the MCFG gives. It must cover no usable RAM either
 *          (physical_memory_manager_reaches_ram): a window maps its frames uncached while the allocator maps RAM
 *          write-back, and a page with two memory types is undefined (Intel SDM vol. 3, §14.12.4). Linux refuses more,
 *          any range the E820 map or the ACPI motherboard resources do not reserve (pci_mmcfg_reserved, in
 *          arch/x86/pci/mmconfig-shared.c); here a range in a hole is mapped uncached by every window, so it is kept.
 *
 * @param base_address The address bus 0 of the window's segment group would have, as the MCFG gives it.
 * @param start_bus    The window's first bus.
 * @param end_bus      Its last bus, at or past @p start_bus.
 * @return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_PAST_REACH or
 *         PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_OVER_RAM when it is refused, in that order of precedence;
 *         PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_USABLE otherwise.
 */
extern PeripheralComponentInterconnectExpressWindow_t
peripheral_component_interconnect_express_judge_window(uint64_t base_address, uint8_t start_bus, uint8_t end_bus);

/**
 * @brief Declares the configuration space of every usable ECAM window the boot's MCFG kept, so that the virtual memory
 *        manager maps it uncached, whatever the memory type range registers say over it.
 *
 * @details Called once, after advanced_configuration_and_power_interface_memory_mapped_configuration_initialize and
 *          before anything maps those frames. Each window is judged and counted by its verdict
 *          (peripheral_component_interconnect_express_judge_window), and a usable one the manager has no room for is
 *          refused too. Accesses go through the declared windows only: before this call, every access takes
 *          configuration mechanism #1 or is refused.
 */
extern void peripheral_component_interconnect_express_initialize(void);

/**
 * @brief Reads one register of a function's configuration space, past its first 256 bytes and in any segment group
 *        too.
 *
 * @details The access goes through the ECAM window peripheral_component_interconnect_express_initialize declared
 *          over @p segment_group and @p bus, which maps the function's 4 KiB once and keeps them mapped while the next
 *          access names the same function. Without a declared window, or when the function's 4 KiB cannot be mapped,
 *          the first 256 bytes of segment group 0 go through configuration mechanism #1, and anything else is
 *          refused. Linux on x86 keeps mechanism #1 for those 256 bytes even with a window (raw_pci_read, in
 *          arch/x86/pci/common.c), against chipsets whose ECAM misbehaves; here a test compares the two mechanisms
 *          over every function's first 256 bytes instead. One processor accesses the configuration space at a time,
 *          as for mechanism #1.
 *
 * @param segment_group The PCI segment group, which _SEG names.
 * @param bus           The bus.
 * @param device        The device, below 32.
 * @param function      The function, below 8.
 * @param offset        The register's offset, which @p width divides, its last byte inside the 4 KiB.
 * @param width         1, 2 or 4 bytes.
 * @param out_value     What the register holds; when the access is refused, @p width bytes of ones, as an absent
 *                      function reads, or 32 bits of ones for a width that is not 1, 2 or 4.
 * @return false when the access is refused: an argument is out of range, or no declared window serves it and it lies
 *         past mechanism #1's reach, an offset past 0xFF or a segment group other than 0.
 */
extern bool peripheral_component_interconnect_express_read(uint16_t segment_group, uint8_t bus, uint8_t device,
                                                           uint8_t function, uint16_t offset, uint8_t width,
                                                           uint32_t *out_value);

/**
 * @brief Writes one register of a function's configuration space, by the path
 *        peripheral_component_interconnect_express_read takes.
 *
 * @param segment_group The PCI segment group.
 * @param bus           The bus.
 * @param device        The device, below 32.
 * @param function      The function, below 8.
 * @param offset        The register's offset, which @p width divides, its last byte inside the 4 KiB.
 * @param width         1, 2 or 4 bytes.
 * @param value         What to write, its low @p width bytes.
 * @return false when the access is refused, for the reasons peripheral_component_interconnect_express_read gives;
 *         nothing is written then.
 */
extern bool peripheral_component_interconnect_express_write(uint16_t segment_group, uint8_t bus, uint8_t device,
                                                            uint8_t function, uint16_t offset, uint8_t width,
                                                            uint32_t value);

/**
 * @brief Whether an ECAM window peripheral_component_interconnect_express_initialize declared covers @p bus of
 *        @p segment_group.
 *
 * @param segment_group The PCI segment group.
 * @param bus           The bus.
 * @return true when a declared window covers it.
 */
extern bool peripheral_component_interconnect_express_reaches(uint16_t segment_group, uint8_t bus);

/**
 * @brief How often an ECAM access named another function than the one before, and moved the window.
 *
 * @return The count since boot.
 */
extern uint32_t peripheral_component_interconnect_express_window_moves(void);

/**
 * @brief The ECAM windows peripheral_component_interconnect_express_initialize declared, and those it refused, by
 *        reason.
 *
 * @return The counts, all 0 before the call and on a machine without an MCFG.
 */
extern const PeripheralComponentInterconnectExpressWindowCounts_t *
peripheral_component_interconnect_express_window_counts(void);
/** @} */

#endif /* KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_H */
