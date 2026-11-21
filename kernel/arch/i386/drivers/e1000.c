#include <kernel/drivers/e1000.h>

#include <kernel/cpu/irq.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pci.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/pinned_memory.h>
#include <kernel/memory/vmm.h>

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

/**
 * @name Registers, from the start of base address register 0 (Intel 317453-006EN, Table 13-2)
 * @{
 */
#define E1000_REGISTER_DEVICE_CONTROL            0x0000u  /**< CTRL. */
#define E1000_REGISTER_DEVICE_STATUS             0x0008u  /**< STATUS. */
#define E1000_REGISTER_EEPROM_READ               0x0014u  /**< EERD. */
#define E1000_REGISTER_MANAGEMENT_DATA_CONTROL   0x0020u  /**< MDIC: the PHY's registers. */
#define E1000_REGISTER_FLOW_CONTROL_ADDRESS_LOW  0x0028u  /**< FCAL. */
#define E1000_REGISTER_FLOW_CONTROL_ADDRESS_HIGH 0x002Cu  /**< FCAH. */
#define E1000_REGISTER_FLOW_CONTROL_TYPE         0x0030u  /**< FCT. */
#define E1000_REGISTER_FLOW_CONTROL_TIMER        0x0170u  /**< FCTTV. */
#define E1000_REGISTER_INTERRUPT_CAUSE_READ      0x00C0u  /**< ICR: cleared when read. */
#define E1000_REGISTER_INTERRUPT_MASK_CLEAR      0x00D8u  /**< IMC. */
#define E1000_REGISTER_RECEIVE_CONTROL           0x0100u  /**< RCTL. */
#define E1000_REGISTER_TRANSMIT_CONTROL          0x0400u  /**< TCTL. */
#define E1000_REGISTER_TRANSMIT_GAP              0x0410u  /**< TIPG. */
#define E1000_REGISTER_RECEIVE_BASE_LOW          0x2800u  /**< RDBAL. */
#define E1000_REGISTER_RECEIVE_BASE_HIGH         0x2804u  /**< RDBAH. */
#define E1000_REGISTER_RECEIVE_LENGTH            0x2808u  /**< RDLEN, in bytes. */
#define E1000_REGISTER_RECEIVE_HEAD              0x2810u  /**< RDH. */
#define E1000_REGISTER_RECEIVE_TAIL              0x2818u  /**< RDT: the receive doorbell. */
#define E1000_REGISTER_RECEIVE_DELAY             0x2820u  /**< RDTR. */
#define E1000_REGISTER_TRANSMIT_BASE_LOW         0x3800u  /**< TDBAL. */
#define E1000_REGISTER_TRANSMIT_BASE_HIGH        0x3804u  /**< TDBAH. */
#define E1000_REGISTER_TRANSMIT_LENGTH           0x3808u  /**< TDLEN, in bytes. */
#define E1000_REGISTER_TRANSMIT_HEAD             0x3810u  /**< TDH. */
#define E1000_REGISTER_TRANSMIT_TAIL             0x3818u  /**< TDT: the transmit doorbell. */
#define E1000_REGISTER_CRC_ERRORS                0x4000u  /**< CRCERRS, cleared when read. */
#define E1000_REGISTER_MISSED_PACKETS            0x4010u  /**< MPC, cleared when read. */
#define E1000_REGISTER_GOOD_PACKETS_RECEIVED     0x4074u  /**< GPRC, cleared when read. */
#define E1000_REGISTER_GOOD_PACKETS_TRANSMITTED  0x4080u  /**< GPTC, cleared when read. */
#define E1000_REGISTER_RECEIVE_NO_BUFFERS        0x40A0u  /**< RNBC, cleared when read. */
#define E1000_REGISTER_MULTICAST_TABLE           0x5200u  /**< MTA[0]; 128 dwords. */
#define E1000_REGISTER_RECEIVE_ADDRESS_LOW       0x5400u  /**< RAL0. */
#define E1000_REGISTER_RECEIVE_ADDRESS_HIGH      0x5404u  /**< RAH0. */
#define E1000_REGISTER_MANAGEMENT_CONTROL        0x5820u  /**< MANC. */
#define E1000_REGISTER_WINDOW_BYTES              0x20000u /**< Bytes of the register window, 128 KiB. */
#define E1000_MULTICAST_TABLE_DWORDS             128u     /**< Dwords of the multicast table array. */
/** @} */

/**
 * @name Bits of CTRL (§13.4.1) and STATUS (§13.4.2)
 * @{
 */
#define E1000_CONTROL_FULL_DUPLEX      (1u << 0)  /**< FD. */
#define E1000_CONTROL_LINK_RESET       (1u << 3)  /**< LRST. */
#define E1000_CONTROL_AUTO_SPEED       (1u << 5)  /**< ASDE: the MAC takes the speed the PHY resolved. */
#define E1000_CONTROL_SET_LINK_UP      (1u << 6)  /**< SLU. */
#define E1000_CONTROL_INVERT_LOSS      (1u << 7)  /**< ILOS. */
#define E1000_CONTROL_SPEED_MASK       (3u << 8)  /**< SPEED. */
#define E1000_CONTROL_SPEED_1000       (2u << 8)  /**< SPEED 10b: 1000 Mb/s. */
#define E1000_CONTROL_FORCE_SPEED      (1u << 11) /**< FRCSPD. */
#define E1000_CONTROL_FORCE_DUPLEX     (1u << 12) /**< FRCDPLX. */
#define E1000_CONTROL_RESET            (1u << 26) /**< RST, self-clearing. */
#define E1000_CONTROL_RECEIVE_PAUSE    (1u << 27) /**< RFCE. */
#define E1000_CONTROL_TRANSMIT_PAUSE   (1u << 28) /**< TFCE. */
#define E1000_CONTROL_VLAN_MODE        (1u << 30) /**< VME. */
#define E1000_CONTROL_PHY_RESET        (1u << 31) /**< PHY_RST. */
#define E1000_STATUS_LINK_UP           (1u << 1)  /**< LU. */
#define E1000_MANAGEMENT_ARP_FILTERING (1u << 13) /**< MANC.ARP_EN, which Linux clears on the 82540 and later. */
/** @} */

/**
 * @name Bits of RCTL (§13.4.22), TCTL (§13.4.33) and TIPG (§13.4.34)
 * @{
 */
#define E1000_RECEIVE_ENABLE      (1u << 1)                  /**< RCTL.EN. */
#define E1000_RECEIVE_BROADCAST   (1u << 15)                 /**< RCTL.BAM. */
#define E1000_RECEIVE_STRIP_CRC   (1u << 26)                 /**< RCTL.SECRC; RCTL.BSIZE 00b gives 2048-byte buffers. */
#define E1000_TRANSMIT_ENABLE     (1u << 1)                  /**< TCTL.EN. */
#define E1000_TRANSMIT_PAD_SHORT  (1u << 3)                  /**< TCTL.PSP. */
#define E1000_TRANSMIT_COLLISIONS (0x10u << 4)               /**< TCTL.CT: 10h, the Ethernet standard. */
#define E1000_TRANSMIT_DISTANCE   (0x40u << 12)              /**< TCTL.COLD: 40h, for full duplex. */
#define E1000_TRANSMIT_GAP_COPPER (8u | 8u << 10 | 6u << 20) /**< IPGT, IPGR1, IPGR2: Linux's copper values. */
/** @} */

/**
 * @name EERD (§13.4.4, its 82540 and 82545 layout) and MDIC (§13.4.7)
 * @{
 */
#define E1000_EEPROM_START              (1u << 0)  /**< START. */
#define E1000_EEPROM_DONE               (1u << 4)  /**< DONE. */
#define E1000_EEPROM_ADDRESS_SHIFT      8u         /**< ADDR, in bits 15:8. */
#define E1000_EEPROM_DATA_SHIFT         16u        /**< DATA, in bits 31:16. */
#define E1000_MANAGEMENT_REGISTER_SHIFT 16u        /**< REGADD, in bits 20:16. */
#define E1000_MANAGEMENT_PHY            (1u << 21) /**< PHYADD 1: the internal PHY. */
#define E1000_MANAGEMENT_WRITE          (1u << 26) /**< OP 01b. */
#define E1000_MANAGEMENT_READ           (2u << 26) /**< OP 10b. */
#define E1000_MANAGEMENT_READY          (1u << 28) /**< R. */
#define E1000_MANAGEMENT_ERROR          (1u << 30) /**< E. */
#define E1000_MANAGEMENT_DATA_MASK      0xFFFFu    /**< DATA. */
/** @} */

/**
 * @name PHY registers: IEEE 802.3 clause 22's control register, and the M88E1011's specific control
 * @{
 */
#define E1000_PHY_CONTROL             0x00u   /**< PHY_CTRL. */
#define E1000_PHY_SPECIFIC_CONTROL    0x10u   /**< M88E1000_PHY_SPEC_CTRL. */
#define E1000_PHY_SPEED_1000          0x0040u /**< Speed select, most significant bit. */
#define E1000_PHY_FULL_DUPLEX         0x0100u /**< Duplex mode. */
#define E1000_PHY_RESTART_NEGOTIATION 0x0200u /**< Restart auto-negotiation, self-clearing. */
#define E1000_PHY_NEGOTIATION         0x1000u /**< Auto-negotiation enable. */
#define E1000_PHY_LOOPBACK            0x4000u /**< Loopback. */
#define E1000_PHY_RESET               0x8000u /**< Reset, self-clearing. */
#define E1000_PHY_LOOPBACK_CROSSOVER  0x0808u /**< Specific control with auto-MDI/MDIX off, as Linux sets it. */
/** @} */

/**
 * @name Descriptor bits (§3.2.3, Table 3-2; §3.3.3, Tables 3-10 and 3-11)
 * @{
 */
#define E1000_RECEIVE_DONE           0x01u /**< RDESC.STATUS.DD. */
#define E1000_TRANSMIT_END_OF_PACKET 0x01u /**< TDESC.CMD.EOP. */
#define E1000_TRANSMIT_INSERT_CRC    0x02u /**< TDESC.CMD.IFCS. */
#define E1000_TRANSMIT_REPORT_STATUS 0x08u /**< TDESC.CMD.RS. */
#define E1000_TRANSMIT_DONE          0x01u /**< TDESC.STA.DD. */
#define E1000_TRANSMIT_COMMAND_SHIFT 24u   /**< CMD, in bits 31:24 of the descriptor's third dword. */
/** @} */

/**
 * @name How long the controller may take
 * @{
 */
#define E1000_QUIESCE_MICROSECONDS     10000u /**< Before the reset, for transactions in flight, as Linux waits. */
#define E1000_RESET_MICROSECONDS       10000u /**< For RST to clear. */
#define E1000_EEPROM_LOAD_MICROSECONDS 5000u  /**< For the EEPROM's auto-read after the reset, as Linux waits. */
#define E1000_EEPROM_READ_MICROSECONDS 10000u /**< For one EEPROM word. */
#define E1000_MANAGEMENT_MICROSECONDS  5000u  /**< For one PHY register; Linux polls 64 times 50 us. */
#define E1000_LOOPBACK_MICROSECONDS    500u   /**< For the PHY to settle in loopback, as Linux waits. */
/** @} */

/** PCI vendor ID of Intel. */
#define E1000_VENDOR 0x8086u

/** Bytes of the page that holds both rings: the receive ring first, then the transmit ring. */
#define E1000_RINGS_BYTES 4096u

/**
 * @brief A legacy receive descriptor (317453-006EN, Table 3-1).
 */
typedef struct E1000ReceiveDescriptor {
    uint64_t buffer_address;                 /**< Physical address of the descriptor's buffer. */
    volatile uint16_t length;                /**< Bytes the controller wrote. */
    volatile uint16_t checksum;              /**< Packet checksum. */
    volatile uint32_t status_errors_special; /**< STATUS in bits 7:0, ERRORS in 15:8, special in 31:16. */
} E1000ReceiveDescriptor_t;

/**
 * @brief A legacy transmit descriptor (317453-006EN, Table 3-8).
 */
typedef struct E1000TransmitDescriptor {
    uint64_t buffer_address;              /**< Physical address of the descriptor's buffer. */
    uint32_t length_and_command;          /**< Length in bits 15:0, CSO in 23:16, CMD in 31:24. */
    volatile uint32_t status_and_special; /**< STA in bits 3:0, CSS in 15:8, special in 31:16. */
} E1000TransmitDescriptor_t;

_Static_assert(sizeof(E1000ReceiveDescriptor_t) == 16u && sizeof(E1000TransmitDescriptor_t) == 16u,
               "a legacy descriptor is 16 bytes");
_Static_assert((E1000_RECEIVE_DESCRIPTORS & (E1000_RECEIVE_DESCRIPTORS - 1u)) == 0u &&
                   (E1000_TRANSMIT_DESCRIPTORS & (E1000_TRANSMIT_DESCRIPTORS - 1u)) == 0u,
               "the free-running indices are masked into the rings");
_Static_assert((E1000_RECEIVE_DESCRIPTORS + E1000_TRANSMIT_DESCRIPTORS) * 16u <= E1000_RINGS_BYTES &&
                   E1000_RECEIVE_DESCRIPTORS * 16u % 128u == 0u && E1000_TRANSMIT_DESCRIPTORS * 16u % 128u == 0u,
               "both rings fit one page, each a multiple of 128 bytes as RDLEN and TDLEN require");
_Static_assert(4096u % E1000_BUFFER_BYTES == 0u, "a buffer never crosses a page");

/**
 * @brief PCI device IDs of the copper 8254x parts with an internal M88 PHY and the EERD register:
 *        the 82540EM, its LOM, the 82540EP and its two variants, the 82545EM and the 82545GM.
 */
static const uint16_t e1000_device_identifiers[] = {0x100Eu, 0x1015u, 0x1016u, 0x1017u, 0x101Eu, 0x100Fu, 0x1026u};

static E1000State_t e1000_found;
static E1000Counters_t e1000_counted;
static volatile uint32_t *e1000_registers = NULL;
static E1000ReceiveDescriptor_t *e1000_receive_ring = NULL;
static E1000TransmitDescriptor_t *e1000_transmit_ring = NULL;
static uint32_t e1000_rings_physical = 0u;
static uint8_t *e1000_receive_buffers = NULL;
static uint8_t *e1000_transmit_buffers = NULL;
static uint32_t e1000_receive_next = 0u;
static uint32_t e1000_receive_ready = 0u;
static uint32_t e1000_transmit_tail = 0u;
static uint32_t e1000_transmit_clean = 0u;
static uint32_t e1000_transmit_reserved = 0u;
static uint32_t e1000_publish_ends[E1000_TRANSMIT_DESCRIPTORS];
static uint32_t e1000_publish_first = 0u;
static uint32_t e1000_publish_count = 0u;
static bool e1000_before_loopback_saved = false;
static uint32_t e1000_device_control_before_loopback = 0u;
static uint16_t e1000_phy_control_before_loopback = 0u;
static uint16_t e1000_phy_specific_control_before_loopback = 0u;
static bool e1000_transmit_tail_held = false;
static bool e1000_transmit_tail_behind = false;

/**
 * @brief Reads a controller register.
 */
static uint32_t e1000_read_register(uint32_t offset)
{
    ++e1000_counted.register_reads;
    return e1000_registers[offset / sizeof(uint32_t)];
}

/**
 * @brief Writes a controller register, after every store the code before it made.
 */
static void e1000_write_register(uint32_t offset, uint32_t value)
{
    ++e1000_counted.register_writes;
    atomic_store_release(&e1000_registers[offset / sizeof(uint32_t)], value);
}

/**
 * @brief Records the step the bring-up stopped at.
 *
 * @return false, for the caller to return.
 */
static bool e1000_fail(const char *step)
{
    e1000_found.failure = step;
    return false;
}

/**
 * @brief Whether @p device is an 8254x of a model this driver drives.
 */
static bool e1000_drives(const PeripheralComponentInterconnectDevice_t *device)
{
    if (device == NULL || device->vendor_id != E1000_VENDOR)
        return false;
    for (uint32_t model = 0u; model < sizeof(e1000_device_identifiers) / sizeof(uint16_t); ++model)
        if (device->device_id == e1000_device_identifiers[model])
            return true;
    return false;
}

/**
 * @brief The first 8254x on the bus of a model this driver drives, or NULL.
 */
static const PeripheralComponentInterconnectDevice_t *e1000_find_controller(void)
{
    const uint32_t device_count = peripheral_component_interconnect_get_device_count();

    for (uint32_t index = 0u; index < device_count; ++index)
    {
        const PeripheralComponentInterconnectDevice_t *const device =
            peripheral_component_interconnect_get_device(index);

        if (e1000_drives(device))
            return device;
    }
    return NULL;
}

/**
 * @brief Waits until the bits @p mask of a register read @p expected, within @p microseconds.
 *
 * @return The last value read, which the caller checks against @p mask.
 */
static uint32_t e1000_wait_register(uint32_t offset, uint32_t mask, uint32_t expected, uint32_t microseconds)
{
    InterruptRequestDeadline_t deadline;
    uint32_t value = e1000_read_register(offset);

    interrupt_request_deadline_start(&deadline, microseconds);
    while ((value & mask) != expected && !interrupt_request_deadline_passed(&deadline))
    {
        asmutils_pause();
        value = e1000_read_register(offset);
    }
    return value;
}

/**
 * @brief Reads one PHY register through MDIC.
 *
 * @return false when the PHY did not answer, or answered with an error.
 */
static bool e1000_read_phy(uint32_t phy_register, uint16_t *out_value)
{
    e1000_write_register(E1000_REGISTER_MANAGEMENT_DATA_CONTROL, phy_register << E1000_MANAGEMENT_REGISTER_SHIFT |
                                                                     E1000_MANAGEMENT_PHY | E1000_MANAGEMENT_READ);

    const uint32_t management = e1000_wait_register(E1000_REGISTER_MANAGEMENT_DATA_CONTROL, E1000_MANAGEMENT_READY,
                                                    E1000_MANAGEMENT_READY, E1000_MANAGEMENT_MICROSECONDS);

    if ((management & E1000_MANAGEMENT_READY) == 0u || (management & E1000_MANAGEMENT_ERROR) != 0u)
        return false;
    *out_value = (uint16_t) (management & E1000_MANAGEMENT_DATA_MASK);
    return true;
}

/**
 * @brief Writes one PHY register through MDIC.
 *
 * @return false when the PHY did not take it.
 */
static bool e1000_write_phy(uint32_t phy_register, uint16_t value)
{
    e1000_write_register(E1000_REGISTER_MANAGEMENT_DATA_CONTROL, (uint32_t) value |
                                                                     phy_register << E1000_MANAGEMENT_REGISTER_SHIFT |
                                                                     E1000_MANAGEMENT_PHY | E1000_MANAGEMENT_WRITE);

    const uint32_t management = e1000_wait_register(E1000_REGISTER_MANAGEMENT_DATA_CONTROL, E1000_MANAGEMENT_READY,
                                                    E1000_MANAGEMENT_READY, E1000_MANAGEMENT_MICROSECONDS);

    return (management & E1000_MANAGEMENT_READY) != 0u && (management & E1000_MANAGEMENT_ERROR) == 0u;
}

/**
 * @brief Reads one EEPROM word through EERD.
 *
 * @return false when the read did not complete.
 */
static bool e1000_read_eeprom(uint32_t word, uint16_t *out_value)
{
    e1000_write_register(E1000_REGISTER_EEPROM_READ, word << E1000_EEPROM_ADDRESS_SHIFT | E1000_EEPROM_START);

    const uint32_t eeprom = e1000_wait_register(E1000_REGISTER_EEPROM_READ, E1000_EEPROM_DONE, E1000_EEPROM_DONE,
                                                E1000_EEPROM_READ_MICROSECONDS);

    if ((eeprom & E1000_EEPROM_DONE) == 0u)
        return false;
    *out_value = (uint16_t) (eeprom >> E1000_EEPROM_DATA_SHIFT);
    return true;
}

/**
 * @brief Reads the clear-on-read statistics registers this driver counts, and adds them.
 */
static void e1000_add_statistics(void)
{
    e1000_counted.cyclic_redundancy_check_errors += e1000_read_register(E1000_REGISTER_CRC_ERRORS);
    e1000_counted.missed += e1000_read_register(E1000_REGISTER_MISSED_PACKETS);
    e1000_counted.good_received += e1000_read_register(E1000_REGISTER_GOOD_PACKETS_RECEIVED);
    e1000_counted.good_transmitted += e1000_read_register(E1000_REGISTER_GOOD_PACKETS_TRANSMITTED);
    e1000_counted.no_buffers += e1000_read_register(E1000_REGISTER_RECEIVE_NO_BUFFERS);
}

/**
 * @brief Reads the clear-on-read statistics registers this driver counts, so that they count from zero.
 */
static void e1000_clear_statistics(void)
{
    (void) e1000_read_register(E1000_REGISTER_CRC_ERRORS);
    (void) e1000_read_register(E1000_REGISTER_MISSED_PACKETS);
    (void) e1000_read_register(E1000_REGISTER_GOOD_PACKETS_RECEIVED);
    (void) e1000_read_register(E1000_REGISTER_GOOD_PACKETS_TRANSMITTED);
    (void) e1000_read_register(E1000_REGISTER_RECEIVE_NO_BUFFERS);
}

/**
 * @brief Maps the register window of @p device, once, and lets it master the bus with its pin interrupt off.
 */
static bool e1000_map_registers(const PeripheralComponentInterconnectDevice_t *device)
{
    PeripheralComponentInterconnectBaseAddressRegister_t window;

    if (e1000_registers != NULL)
        return true;
    if (!peripheral_component_interconnect_read_base_address_register(device->bus, device->device, device->function, 0u,
                                                                      &window) ||
        !peripheral_component_interconnect_memory_window_is_reachable(&window) ||
        window.size < E1000_REGISTER_WINDOW_BYTES)
        return false;

    const uint32_t mapped = kernel_vmm_map_device_window((uint32_t) window.base, (uint32_t) window.size);

    e1000_registers = (volatile uint32_t *) (uintptr_t) mapped;
    return mapped != 0u;
}

/**
 * @brief The physical address of @p virtual_address, in pinned memory, or 0 when it has none.
 */
static uint32_t e1000_physical_address(const void *virtual_address)
{
    uint32_t physical = 0u;

    return paging_get_physical_address((uint32_t) (uintptr_t) virtual_address, &physical) ? physical : 0u;
}

/**
 * @brief Claims, once, the page of both rings and every buffer, in pinned memory.
 */
static bool e1000_claim_memory(void)
{
    if (e1000_receive_ring == NULL)
    {
        uint8_t *const rings = (uint8_t *) kernel_pinned_alloc(E1000_RINGS_BYTES);

        e1000_rings_physical = e1000_physical_address(rings);
        if (rings == NULL || e1000_rings_physical == 0u)
            return false;
        e1000_receive_ring = (E1000ReceiveDescriptor_t *) rings;
        e1000_transmit_ring = (E1000TransmitDescriptor_t *) (rings + E1000_RECEIVE_DESCRIPTORS * 16u);
    }
    if (e1000_receive_buffers == NULL)
        e1000_receive_buffers = (uint8_t *) kernel_pinned_alloc(E1000_RECEIVE_DESCRIPTORS * E1000_BUFFER_BYTES);
    if (e1000_transmit_buffers == NULL)
        e1000_transmit_buffers = (uint8_t *) kernel_pinned_alloc(E1000_TRANSMIT_DESCRIPTORS * E1000_BUFFER_BYTES);
    return e1000_receive_buffers != NULL && e1000_transmit_buffers != NULL;
}

/**
 * @brief Gives every descriptor of both rings its buffer, with nothing received and nothing to send.
 *
 * @return false when a buffer has no physical address.
 */
static bool e1000_fill_rings(void)
{
    memset(e1000_receive_ring, 0, E1000_RINGS_BYTES);
    for (uint32_t descriptor = 0u; descriptor < E1000_RECEIVE_DESCRIPTORS; ++descriptor)
    {
        const uint32_t physical = e1000_physical_address(e1000_receive_buffers + descriptor * E1000_BUFFER_BYTES);

        if (physical == 0u)
            return false;
        e1000_receive_ring[descriptor].buffer_address = physical;
    }
    for (uint32_t descriptor = 0u; descriptor < E1000_TRANSMIT_DESCRIPTORS; ++descriptor)
    {
        const uint32_t physical = e1000_physical_address(e1000_transmit_buffers + descriptor * E1000_BUFFER_BYTES);

        if (physical == 0u)
            return false;
        e1000_transmit_ring[descriptor].buffer_address = physical;
    }
    e1000_receive_next = 0u;
    e1000_receive_ready = 0u;
    e1000_transmit_tail = 0u;
    e1000_transmit_clean = 0u;
    e1000_transmit_reserved = 0u;
    e1000_publish_first = 0u;
    e1000_publish_count = 0u;
    return true;
}

/**
 * @brief Stops the controller's receiver and transmitter, then resets it (317453-006EN, §14.7), as
 *        Linux's e1000_reset_hw does for the 82540 and 82545.
 */
static bool e1000_reset(void)
{
    e1000_write_register(E1000_REGISTER_INTERRUPT_MASK_CLEAR, 0xFFFFFFFFu);
    e1000_write_register(E1000_REGISTER_RECEIVE_CONTROL, 0u);
    e1000_write_register(E1000_REGISTER_TRANSMIT_CONTROL, E1000_TRANSMIT_PAD_SHORT);
    (void) e1000_read_register(E1000_REGISTER_DEVICE_STATUS);
    interrupt_request_spin_microseconds(E1000_QUIESCE_MICROSECONDS);

    e1000_write_register(E1000_REGISTER_DEVICE_CONTROL,
                         e1000_read_register(E1000_REGISTER_DEVICE_CONTROL) | E1000_CONTROL_RESET);
    interrupt_request_spin_microseconds(1u);
    if ((e1000_wait_register(E1000_REGISTER_DEVICE_CONTROL, E1000_CONTROL_RESET, 0u, E1000_RESET_MICROSECONDS) &
         E1000_CONTROL_RESET) != 0u)
        return false;
    interrupt_request_spin_microseconds(E1000_EEPROM_LOAD_MICROSECONDS);

    e1000_write_register(E1000_REGISTER_INTERRUPT_MASK_CLEAR, 0xFFFFFFFFu);
    (void) e1000_read_register(E1000_REGISTER_INTERRUPT_CAUSE_READ);
    e1000_write_register(E1000_REGISTER_MANAGEMENT_CONTROL,
                         e1000_read_register(E1000_REGISTER_MANAGEMENT_CONTROL) & ~E1000_MANAGEMENT_ARP_FILTERING);
    return true;
}

/**
 * @brief Reads the station address from EEPROM words 0 to 2 and makes it receive address 0, with
 *        the multicast table empty (§5.6.1, §14.4).
 */
static bool e1000_program_address(void)
{
    uint16_t words[E1000_EEPROM_ADDRESS_WORDS];

    for (uint32_t word = 0u; word < E1000_EEPROM_ADDRESS_WORDS; ++word)
        if (!e1000_read_eeprom(word, &words[word]))
            return false;
    e1000_station_address_from_eeprom(words, e1000_found.address);

    const uint8_t *const address = e1000_found.address;

    e1000_write_register(E1000_REGISTER_RECEIVE_ADDRESS_LOW, (uint32_t) address[0] | (uint32_t) address[1] << 8 |
                                                                 (uint32_t) address[2] << 16 |
                                                                 (uint32_t) address[3] << 24);
    e1000_write_register(E1000_REGISTER_RECEIVE_ADDRESS_HIGH,
                         (uint32_t) address[4] | (uint32_t) address[5] << 8 | 0x80000000u);
    for (uint32_t dword = 0u; dword < E1000_MULTICAST_TABLE_DWORDS; ++dword)
        e1000_write_register(E1000_REGISTER_MULTICAST_TABLE + dword * sizeof(uint32_t), 0u);
    return true;
}

/**
 * @brief Reads from the PHY whether it is in loopback, into the state.
 */
static bool e1000_read_loopback(void)
{
    uint16_t phy_control = 0u;

    if (!e1000_read_phy(E1000_PHY_CONTROL, &phy_control))
        return false;
    e1000_found.loopback = (phy_control & E1000_PHY_LOOPBACK) != 0u;
    return true;
}

/**
 * @brief Gives the PHY back the control it had before the loopback, or takes it out of a loopback an
 *        earlier boot left, and lets it negotiate again.
 */
static bool e1000_leave_loopback(void)
{
    uint16_t phy_control = 0u;

    if (!e1000_read_phy(E1000_PHY_CONTROL, &phy_control))
        return false;
    if (e1000_before_loopback_saved)
    {
        phy_control = e1000_phy_control_before_loopback;
        e1000_write_register(E1000_REGISTER_DEVICE_CONTROL, e1000_device_control_before_loopback);
        if (!e1000_write_phy(E1000_PHY_SPECIFIC_CONTROL, e1000_phy_specific_control_before_loopback))
            return false;
        e1000_before_loopback_saved = false;
    }
    else if ((phy_control & E1000_PHY_LOOPBACK) == 0u)
        return e1000_read_loopback();
    return e1000_write_phy(E1000_PHY_CONTROL, (uint16_t) ((phy_control & ~(E1000_PHY_LOOPBACK | E1000_PHY_RESET)) |
                                                          E1000_PHY_NEGOTIATION | E1000_PHY_RESTART_NEGOTIATION)) &&
           e1000_read_loopback() && !e1000_found.loopback;
}

/**
 * @brief Sets the link up with the speed the PHY resolves and no flow control (§14.3), and takes the
 *        PHY out of loopback.
 */
static bool e1000_configure_link(void)
{
    const uint32_t control = e1000_read_register(E1000_REGISTER_DEVICE_CONTROL);

    e1000_write_register(E1000_REGISTER_DEVICE_CONTROL,
                         (control | E1000_CONTROL_SET_LINK_UP | E1000_CONTROL_AUTO_SPEED) &
                             ~(E1000_CONTROL_LINK_RESET | E1000_CONTROL_PHY_RESET | E1000_CONTROL_INVERT_LOSS |
                               E1000_CONTROL_VLAN_MODE | E1000_CONTROL_FORCE_SPEED | E1000_CONTROL_FORCE_DUPLEX |
                               E1000_CONTROL_RECEIVE_PAUSE | E1000_CONTROL_TRANSMIT_PAUSE));
    e1000_write_register(E1000_REGISTER_FLOW_CONTROL_ADDRESS_LOW, 0u);
    e1000_write_register(E1000_REGISTER_FLOW_CONTROL_ADDRESS_HIGH, 0u);
    e1000_write_register(E1000_REGISTER_FLOW_CONTROL_TYPE, 0u);
    e1000_write_register(E1000_REGISTER_FLOW_CONTROL_TIMER, 0u);
    e1000_before_loopback_saved = false;
    return e1000_leave_loopback();
}

/**
 * @brief Hands both rings to the controller and turns its receiver and transmitter on (§14.4, §14.5).
 */
static void e1000_start_rings(void)
{
    e1000_write_register(E1000_REGISTER_RECEIVE_BASE_LOW, e1000_rings_physical);
    e1000_write_register(E1000_REGISTER_RECEIVE_BASE_HIGH, 0u);
    e1000_write_register(E1000_REGISTER_RECEIVE_LENGTH, E1000_RECEIVE_DESCRIPTORS * 16u);
    e1000_write_register(E1000_REGISTER_RECEIVE_HEAD, 0u);
    e1000_write_register(E1000_REGISTER_RECEIVE_TAIL, E1000_RECEIVE_DESCRIPTORS - 1u);
    e1000_write_register(E1000_REGISTER_RECEIVE_DELAY, 0u);
    e1000_write_register(E1000_REGISTER_TRANSMIT_BASE_LOW, e1000_rings_physical + E1000_RECEIVE_DESCRIPTORS * 16u);
    e1000_write_register(E1000_REGISTER_TRANSMIT_BASE_HIGH, 0u);
    e1000_write_register(E1000_REGISTER_TRANSMIT_LENGTH, E1000_TRANSMIT_DESCRIPTORS * 16u);
    e1000_write_register(E1000_REGISTER_TRANSMIT_HEAD, 0u);
    e1000_write_register(E1000_REGISTER_TRANSMIT_TAIL, 0u);
    e1000_write_register(E1000_REGISTER_TRANSMIT_GAP, E1000_TRANSMIT_GAP_COPPER);
    e1000_write_register(E1000_REGISTER_TRANSMIT_CONTROL, E1000_TRANSMIT_ENABLE | E1000_TRANSMIT_PAD_SHORT |
                                                              E1000_TRANSMIT_COLLISIONS | E1000_TRANSMIT_DISTANCE);
    e1000_write_register(E1000_REGISTER_RECEIVE_CONTROL,
                         E1000_RECEIVE_ENABLE | E1000_RECEIVE_BROADCAST | E1000_RECEIVE_STRIP_CRC);
}

/**
 * @brief Marks as free the transmit descriptors of every publish the controller reported done.
 */
static void e1000_reclaim_transmitted(void)
{
    while (e1000_publish_count != 0u)
    {
        const uint32_t last = e1000_publish_ends[e1000_publish_first];
        const E1000TransmitDescriptor_t *const descriptor =
            &e1000_transmit_ring[last & (E1000_TRANSMIT_DESCRIPTORS - 1u)];

        if ((atomic_load_acquire(&descriptor->status_and_special) & E1000_TRANSMIT_DONE) == 0u)
            return;
        e1000_transmit_clean = last + 1u;
        e1000_publish_first = (e1000_publish_first + 1u) & (E1000_TRANSMIT_DESCRIPTORS - 1u);
        --e1000_publish_count;
    }
}

/**
 * @brief Whether a publish may send @p count frames of @p lengths.
 */
static bool e1000_publish_is_valid(const uint16_t *lengths, uint32_t count)
{
    if (count > e1000_transmit_reserved || lengths == NULL || !e1000_found.controller_ready)
        return false;
    for (uint32_t frame = 0u; frame < count; ++frame)
        if (lengths[frame] < E1000_FRAME_LEAST_BYTES || lengths[frame] > E1000_FRAME_MOST_BYTES)
            return false;
    return true;
}

bool e1000_initialize(void)
{
    const PeripheralComponentInterconnectDevice_t *const device = e1000_find_controller();

    memset(&e1000_found, 0, sizeof(e1000_found));
    memset(&e1000_counted, 0, sizeof(e1000_counted));
    e1000_receive_ready = 0u;
    e1000_transmit_reserved = 0u;
    e1000_transmit_tail_behind = false;
    if (device == NULL)
        return e1000_fail("no 8254x of a model this driver drives on the bus");
    e1000_found.controller_present = true;
    e1000_found.device_identifier = device->device_id;

    if (!e1000_map_registers(device))
        return e1000_fail("its register window is out of reach");
    peripheral_component_interconnect_enable_memory_and_bus_mastering(device);
    peripheral_component_interconnect_disable_pin_interrupt(device);
    if (!e1000_claim_memory())
        return e1000_fail("no pinned memory for its rings and buffers");
    e1000_found.receive_buffers = e1000_receive_buffers;
    e1000_found.receive_buffers_bytes = E1000_RECEIVE_DESCRIPTORS * E1000_BUFFER_BYTES;

    if (!e1000_reset())
        return e1000_fail("it did not come out of reset");
    if (!e1000_program_address())
        return e1000_fail("its EEPROM did not give the station address");
    if (!e1000_configure_link())
        return e1000_fail("its PHY did not answer");
    if (!e1000_fill_rings())
        return e1000_fail("a buffer has no physical address");
    e1000_clear_statistics();
    e1000_start_rings();

    e1000_found.link_up = (e1000_read_register(E1000_REGISTER_DEVICE_STATUS) & E1000_STATUS_LINK_UP) != 0u;
    e1000_found.controller_ready = true;
    return true;
}

const E1000State_t *e1000_state(void) { return &e1000_found; }

void e1000_decode_receive_status(uint32_t status_errors_special, E1000ReceivedFrame_t *frame)
{
    frame->status = (uint8_t) status_errors_special;
    frame->errors = (uint8_t) (status_errors_special >> 8);
}

void e1000_station_address_from_eeprom(const uint16_t *words, uint8_t *address)
{
    for (uint32_t word = 0u; word < E1000_EEPROM_ADDRESS_WORDS; ++word)
    {
        address[2u * word] = (uint8_t) words[word];
        address[2u * word + 1u] = (uint8_t) (words[word] >> 8);
    }
}

bool e1000_set_loopback(bool enabled)
{
    if (!e1000_found.controller_ready)
        return false;
    if (!enabled)
        return e1000_leave_loopback();
    if (e1000_found.loopback)
        return true;
    if (!e1000_read_phy(E1000_PHY_CONTROL, &e1000_phy_control_before_loopback) ||
        !e1000_read_phy(E1000_PHY_SPECIFIC_CONTROL, &e1000_phy_specific_control_before_loopback))
        return false;
    e1000_device_control_before_loopback = e1000_read_register(E1000_REGISTER_DEVICE_CONTROL);
    e1000_before_loopback_saved = true;

    if (!e1000_write_phy(E1000_PHY_SPECIFIC_CONTROL, E1000_PHY_LOOPBACK_CROSSOVER) ||
        !e1000_write_phy(E1000_PHY_CONTROL,
                         E1000_PHY_RESET | E1000_PHY_NEGOTIATION | E1000_PHY_FULL_DUPLEX | E1000_PHY_SPEED_1000) ||
        !e1000_write_phy(E1000_PHY_CONTROL, E1000_PHY_RESET | E1000_PHY_FULL_DUPLEX | E1000_PHY_SPEED_1000) ||
        !e1000_write_phy(E1000_PHY_CONTROL, E1000_PHY_LOOPBACK | E1000_PHY_FULL_DUPLEX | E1000_PHY_SPEED_1000))
        return false;
    e1000_write_register(E1000_REGISTER_DEVICE_CONTROL,
                         (e1000_device_control_before_loopback & ~E1000_CONTROL_SPEED_MASK) |
                             E1000_CONTROL_FORCE_SPEED | E1000_CONTROL_FORCE_DUPLEX | E1000_CONTROL_SPEED_1000 |
                             E1000_CONTROL_FULL_DUPLEX | E1000_CONTROL_INVERT_LOSS);
    interrupt_request_spin_microseconds(E1000_LOOPBACK_MICROSECONDS);
    return e1000_read_loopback() && e1000_found.loopback;
}

uint32_t e1000_receive_peek(E1000ReceivedFrame_t *frames, uint32_t wanted)
{
    if (!e1000_found.controller_ready || frames == NULL)
        return 0u;
    while (e1000_receive_ready < wanted && e1000_receive_ready < E1000_RECEIVE_DESCRIPTORS - 1u)
    {
        const E1000ReceiveDescriptor_t *const descriptor =
            &e1000_receive_ring[(e1000_receive_next + e1000_receive_ready) & (E1000_RECEIVE_DESCRIPTORS - 1u)];

        if ((atomic_load_acquire(&descriptor->status_errors_special) & E1000_RECEIVE_DONE) == 0u)
            break;
        ++e1000_receive_ready;
    }

    const uint32_t handed = e1000_receive_ready < wanted ? e1000_receive_ready : wanted;

    for (uint32_t frame = 0u; frame < handed; ++frame)
    {
        const uint32_t index = (e1000_receive_next + frame) & (E1000_RECEIVE_DESCRIPTORS - 1u);
        const E1000ReceiveDescriptor_t *const descriptor = &e1000_receive_ring[index];

        frames[frame].bytes = e1000_receive_buffers + index * E1000_BUFFER_BYTES;
        frames[frame].length = descriptor->length;
        e1000_decode_receive_status(descriptor->status_errors_special, &frames[frame]);
    }
    return handed;
}

bool e1000_receive_release(uint32_t count)
{
    if (count > e1000_receive_ready || (count != 0u && !e1000_found.controller_ready))
    {
        ++e1000_counted.refused;
        return false;
    }
    if (count == 0u)
        return true;
    for (uint32_t frame = 0u; frame < count; ++frame)
        e1000_receive_ring[(e1000_receive_next + frame) & (E1000_RECEIVE_DESCRIPTORS - 1u)].status_errors_special = 0u;
    e1000_receive_next += count;
    e1000_receive_ready -= count;
    e1000_counted.frames_received += count;
    ++e1000_counted.receive_doorbells;
    e1000_write_register(E1000_REGISTER_RECEIVE_TAIL, (e1000_receive_next - 1u) & (E1000_RECEIVE_DESCRIPTORS - 1u));
    return true;
}

uint32_t e1000_transmit_reserve(uint8_t **buffers, uint32_t wanted)
{
    if (!e1000_found.controller_ready || buffers == NULL)
        return 0u;
    e1000_reclaim_transmitted();

    const uint32_t free_descriptors = E1000_TRANSMIT_DESCRIPTORS - 1u - (e1000_transmit_tail - e1000_transmit_clean);
    const uint32_t handed = free_descriptors < wanted ? free_descriptors : wanted;

    for (uint32_t buffer = 0u; buffer < handed; ++buffer)
        buffers[buffer] = e1000_transmit_buffers +
                          ((e1000_transmit_tail + buffer) & (E1000_TRANSMIT_DESCRIPTORS - 1u)) * E1000_BUFFER_BYTES;
    e1000_transmit_reserved = handed;
    return handed;
}

bool e1000_transmit_publish(const uint16_t *lengths, uint32_t count)
{
    if (count == 0u)
        return true;
    if (!e1000_publish_is_valid(lengths, count))
    {
        ++e1000_counted.refused;
        return false;
    }
    for (uint32_t frame = 0u; frame < count; ++frame)
    {
        E1000TransmitDescriptor_t *const descriptor =
            &e1000_transmit_ring[(e1000_transmit_tail + frame) & (E1000_TRANSMIT_DESCRIPTORS - 1u)];
        const uint32_t command = E1000_TRANSMIT_END_OF_PACKET | E1000_TRANSMIT_INSERT_CRC |
                                 (frame + 1u == count ? E1000_TRANSMIT_REPORT_STATUS : 0u);

        descriptor->length_and_command = (uint32_t) lengths[frame] | command << E1000_TRANSMIT_COMMAND_SHIFT;
        descriptor->status_and_special = 0u;
    }
    e1000_transmit_tail += count;
    e1000_publish_ends[(e1000_publish_first + e1000_publish_count) & (E1000_TRANSMIT_DESCRIPTORS - 1u)] =
        e1000_transmit_tail - 1u;
    ++e1000_publish_count;
    e1000_transmit_reserved = 0u;
    e1000_counted.frames_sent += count;
    if (e1000_transmit_tail_held)
    {
        e1000_transmit_tail_behind = true;
        return true;
    }
    ++e1000_counted.transmit_doorbells;
    e1000_write_register(E1000_REGISTER_TRANSMIT_TAIL, e1000_transmit_tail & (E1000_TRANSMIT_DESCRIPTORS - 1u));
    return true;
}

const E1000Counters_t *e1000_counters(void)
{
    if (e1000_found.controller_ready)
        e1000_add_statistics();
    return &e1000_counted;
}

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)
void e1000_debug_hold_transmit_tail(bool held)
{
    e1000_transmit_tail_held = held;
    if (held || !e1000_transmit_tail_behind || !e1000_found.controller_ready)
        return;
    e1000_transmit_tail_behind = false;
    ++e1000_counted.transmit_doorbells;
    e1000_write_register(E1000_REGISTER_TRANSMIT_TAIL, e1000_transmit_tail & (E1000_TRANSMIT_DESCRIPTORS - 1u));
}
#endif
