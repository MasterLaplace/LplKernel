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
#define E1000_STATISTICS_REGISTERS               5u       /**< Statistics registers the counters add. */
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
#define E1000_RECEIVE_ENABLE      (1u << 1)     /**< RCTL.EN. */
#define E1000_RECEIVE_BROADCAST   (1u << 15)    /**< RCTL.BAM. */
#define E1000_RECEIVE_STRIP_CRC   (1u << 26)    /**< RCTL.SECRC; RCTL.BSIZE 00b gives 2048-byte buffers. */
#define E1000_TRANSMIT_ENABLE     (1u << 1)     /**< TCTL.EN. */
#define E1000_TRANSMIT_PAD_SHORT  (1u << 3)     /**< TCTL.PSP. */
#define E1000_TRANSMIT_COLLISIONS (0x10u << 4)  /**< TCTL.CT: 10h, the Ethernet standard. */
#define E1000_TRANSMIT_DISTANCE   (0x40u << 12) /**< TCTL.COLD: 40h, for full duplex. */
#define E1000_TRANSMIT_GAP_COPPER (10u | 8u << 10 | 6u << 20) /**< IPGT, IPGR1, IPGR2 for 1000BASE-T (§13.4.34). */
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
#define E1000_PHY_RESET_MICROSECONDS   1u     /**< After a reset of the PHY, as Linux's e1000_phy_reset waits. */
/** @} */

/** PCI vendor ID of Intel. */
#define E1000_VENDOR 0x8086u

/** Bytes of the page that holds both rings: the receive ring first, then the transmit ring. */
#define E1000_RINGS_BYTES 4096u

_Static_assert(sizeof(E1000ReceiveDescriptor_t) == 16u && sizeof(E1000TransmitDescriptor_t) == 16u,
               "a legacy descriptor is 16 bytes");
_Static_assert(E1000_TRANSMIT_FRAME_COMMAND == (E1000_TRANSMIT_END_OF_PACKET | E1000_TRANSMIT_INSERT_CRC)
                                                   << E1000_TRANSMIT_COMMAND_SHIFT,
               "a described frame's command is EOP and IFCS");
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

/**
 * @brief The statistics registers the counters add, each read once a call.
 */
static const uint32_t e1000_statistics_registers[E1000_STATISTICS_REGISTERS] = {
    E1000_REGISTER_CRC_ERRORS, E1000_REGISTER_MISSED_PACKETS, E1000_REGISTER_GOOD_PACKETS_RECEIVED,
    E1000_REGISTER_GOOD_PACKETS_TRANSMITTED, E1000_REGISTER_RECEIVE_NO_BUFFERS};

static E1000State_t e1000_found;
static E1000Counters_t e1000_counted;
static volatile uint32_t *e1000_registers = NULL;
static E1000ReceiveDescriptor_t *e1000_receive_ring = NULL;
static E1000TransmitDescriptor_t *e1000_transmit_ring = NULL;
static uint32_t e1000_rings_physical = 0u;
static uint8_t *e1000_receive_buffers = NULL;
static uint8_t *e1000_transmit_buffers = NULL;
static E1000Receive_t e1000_receive_side;
static E1000Transmit_t e1000_transmit_side;
static uint32_t e1000_receive_given_back = 0u;
static uint32_t e1000_transmit_kicked = 0u;
static uint32_t e1000_kick_ends[E1000_TRANSMIT_DESCRIPTORS];
static uint32_t e1000_kick_first = 0u;
static uint32_t e1000_kick_count = 0u;
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
    for (uint32_t statistic = 0u; statistic < E1000_STATISTICS_REGISTERS; ++statistic)
        (void) e1000_add_statistics_register(&e1000_counted, e1000_statistics_registers[statistic],
                                             e1000_read_register(e1000_statistics_registers[statistic]));
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
 * @brief Claims @p bytes of pinned memory, zeroed: the pages come from the physical allocator as their
 *        last owner left them, and a publish longer than what its caller wrote must send zeros, not them.
 */
static uint8_t *e1000_claim_zeroed(uint32_t bytes)
{
    uint8_t *const memory = (uint8_t *) kernel_pinned_alloc(bytes);

    if (memory != NULL)
        memset(memory, 0, bytes);
    return memory;
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
        e1000_receive_buffers = e1000_claim_zeroed(E1000_RECEIVE_DESCRIPTORS * E1000_BUFFER_BYTES);
    if (e1000_transmit_buffers == NULL)
        e1000_transmit_buffers = e1000_claim_zeroed(E1000_TRANSMIT_DESCRIPTORS * E1000_BUFFER_BYTES);
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
    e1000_receive_side.descriptors = e1000_receive_ring;
    e1000_receive_side.buffers = e1000_receive_buffers;
    e1000_transmit_side.descriptors = e1000_transmit_ring;
    e1000_transmit_side.buffers = e1000_transmit_buffers;
    e1000_receive_given_back = 0u;
    e1000_transmit_kicked = 0u;
    e1000_kick_first = 0u;
    e1000_kick_count = 0u;
    return true;
}

/**
 * @brief Stops the controller's receiver and transmitter, then resets it (317453-006EN, §14.7).
 *
 * @details The order and the waits are those of Linux's e1000_reset_hw. The reset itself is written
 *          through the register window: Linux writes it through the I/O window on the 82540 and 82545,
 *          which "can't ack the 64-bit write", and through CTRL_DUP on the 82545GM, neither done here.
 *          QEMU ignores RST, so no test sees this path.
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
    uint16_t words[E1000_STATION_ADDRESS_WORDS];

    for (uint32_t word = 0u; word < E1000_STATION_ADDRESS_WORDS; ++word)
        if (!e1000_read_eeprom(word, &words[word]))
            return false;
    e1000_station_address_from_words(words, e1000_found.address);

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
 *        earlier boot left, then resets it so that its specific control takes effect, as Linux's
 *        e1000_loopback_cleanup does, and lets it negotiate again.
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
    if (!e1000_write_phy(E1000_PHY_CONTROL, (uint16_t) ((phy_control & ~E1000_PHY_LOOPBACK) | E1000_PHY_RESET |
                                                        E1000_PHY_NEGOTIATION | E1000_PHY_RESTART_NEGOTIATION)))
        return false;
    interrupt_request_spin_microseconds(E1000_PHY_RESET_MICROSECONDS);
    return e1000_read_loopback() && !e1000_found.loopback;
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
 * @brief Gives the transmit ring back the slots of every kick the controller reported done: DD on a kick's
 *        last descriptor, the one that asked for a report, covers every descriptor before it (§3.3.3.2).
 */
static void e1000_reclaim_transmitted(void)
{
    while (e1000_kick_count != 0u)
    {
        const uint32_t last = e1000_kick_ends[e1000_kick_first];
        const E1000TransmitDescriptor_t *const descriptor =
            &e1000_transmit_ring[last & (E1000_TRANSMIT_DESCRIPTORS - 1u)];

        if ((atomic_load_acquire(&descriptor->status_and_special) & E1000_TRANSMIT_DONE) == 0u)
            return;
        kernel_spsc_ring_release(&e1000_transmit_side.ring, last + 1u - e1000_transmit_side.ring.consumer.read_index);
        e1000_kick_first = (e1000_kick_first + 1u) & (E1000_TRANSMIT_DESCRIPTORS - 1u);
        --e1000_kick_count;
    }
}

/**
 * @brief Takes the receive descriptors from slot @p first_slot on, at most @p most short of the wrap, that the
 *        controller wrote: those with DD set, up to the first without, each one's DD cleared as it is read.
 *
 * @details DD is read relaxed, as Linux reads it before its dma_rmb(): x86 keeps the loads in order, and the
 *          release store that publishes the descriptors orders them before the consumer's reads of the frames.
 *          Clearing it here, with the status in hand, spares the give-back a pass over the descriptors.
 *
 * @return How many were taken.
 */
static uint32_t e1000_take_received(uint32_t first_slot, uint32_t most)
{
    E1000ReceiveDescriptor_t *const first = &e1000_receive_ring[first_slot];
    const E1000ReceiveDescriptor_t *const end = first + most;
    E1000ReceiveDescriptor_t *descriptor = first;

    for (; descriptor != end; ++descriptor)
    {
        const uint32_t status_errors_special = atomic_load_relaxed(&descriptor->status_errors_special);

        if ((status_errors_special & E1000_RECEIVE_DONE) == 0u)
            break;
        descriptor->status_errors_special = status_errors_special & ~E1000_RECEIVE_DONE;
    }
    return (uint32_t) (descriptor - first);
}

/**
 * @brief Gives the controller back the receive slots the consumer released since the last harvest, the tail
 *        one slot behind the oldest held; the harvest cleared their DD bit when it took them.
 */
static void e1000_give_back_received(void)
{
    const uint32_t read_index = atomic_load_acquire(&e1000_receive_side.ring.consumer.read_index);

    if (read_index == e1000_receive_given_back)
        return;
    e1000_counted.frames_received += read_index - e1000_receive_given_back;
    e1000_receive_given_back = read_index;
    ++e1000_counted.receive_doorbells;
    e1000_write_register(E1000_REGISTER_RECEIVE_TAIL, (read_index - 1u) & (E1000_RECEIVE_DESCRIPTORS - 1u));
}

bool e1000_initialize(void)
{
    const PeripheralComponentInterconnectDevice_t *const device = peripheral_component_interconnect_find_by_identifiers(
        E1000_VENDOR, e1000_device_identifiers, sizeof(e1000_device_identifiers) / sizeof(e1000_device_identifiers[0]));

    memset(&e1000_found, 0, sizeof(e1000_found));
    memset(&e1000_counted, 0, sizeof(e1000_counted));
    e1000_transmit_tail_behind = false;
    (void) kernel_spsc_ring_initialize(&e1000_receive_side.ring, E1000_RECEIVE_DESCRIPTORS);
    (void) kernel_spsc_ring_initialize(&e1000_transmit_side.ring, E1000_TRANSMIT_DESCRIPTORS);
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

    if (!e1000_reset())
        return e1000_fail("it did not come out of reset");
    if (!e1000_program_address())
        return e1000_fail("its EEPROM did not give the station address");
    if (!e1000_configure_link())
        return e1000_fail(e1000_found.loopback ? "its PHY stayed in loopback" : "its PHY did not answer");
    if (!e1000_fill_rings())
        return e1000_fail("a buffer has no physical address");
    e1000_clear_statistics();
    e1000_start_rings();

    e1000_found.link_up = (e1000_read_register(E1000_REGISTER_DEVICE_STATUS) & E1000_STATUS_LINK_UP) != 0u;
    e1000_found.controller_ready = true;
    return true;
}

const E1000State_t *e1000_state(void) { return &e1000_found; }

bool e1000_add_statistics_register(E1000Counters_t *counters, uint32_t register_offset, uint32_t value)
{
    switch (register_offset)
    {
    case E1000_REGISTER_CRC_ERRORS: counters->cyclic_redundancy_check_errors += value; return true;
    case E1000_REGISTER_MISSED_PACKETS: counters->missed += value; return true;
    case E1000_REGISTER_GOOD_PACKETS_RECEIVED: counters->good_received += value; return true;
    case E1000_REGISTER_GOOD_PACKETS_TRANSMITTED: counters->good_transmitted += value; return true;
    case E1000_REGISTER_RECEIVE_NO_BUFFERS: counters->no_buffers += value; return true;
    default: return false;
    }
}

bool e1000_link_up(void)
{
    if (!e1000_found.controller_ready)
        return false;
    e1000_found.link_up = (e1000_read_register(E1000_REGISTER_DEVICE_STATUS) & E1000_STATUS_LINK_UP) != 0u;
    return e1000_found.link_up;
}

void e1000_station_address_from_words(const uint16_t *words, uint8_t *address)
{
    for (uint32_t word = 0u; word < E1000_STATION_ADDRESS_WORDS; ++word)
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
    if (!e1000_before_loopback_saved)
    {
        if (!e1000_read_phy(E1000_PHY_CONTROL, &e1000_phy_control_before_loopback) ||
            !e1000_read_phy(E1000_PHY_SPECIFIC_CONTROL, &e1000_phy_specific_control_before_loopback))
            return false;
        e1000_device_control_before_loopback = e1000_read_register(E1000_REGISTER_DEVICE_CONTROL);
        e1000_before_loopback_saved = true;
    }

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

E1000Receive_t *e1000_receive(void) { return &e1000_receive_side; }

uint32_t e1000_receive_harvest(void)
{
    if (!e1000_found.controller_ready)
        return 0u;
    e1000_give_back_received();

    KernelSpscRing_t *const ring = &e1000_receive_side.ring;
    const uint32_t write_index = ring->producer.write_index;
    const uint32_t room = e1000_receive_given_back + (E1000_RECEIVE_DESCRIPTORS - 1u) - write_index;
    const uint32_t first_slot = write_index & (E1000_RECEIVE_DESCRIPTORS - 1u);
    const uint32_t until_wrap = E1000_RECEIVE_DESCRIPTORS - first_slot;
    uint32_t arrived = e1000_take_received(first_slot, room < until_wrap ? room : until_wrap);

    if (arrived == until_wrap && room > until_wrap)
        arrived += e1000_take_received(0u, room - until_wrap);
    if (arrived == room)
        ++e1000_counted.receive_ring_full_harvests;
    kernel_spsc_ring_publish(ring, arrived);
    return arrived;
}

E1000Transmit_t *e1000_transmit(void) { return &e1000_transmit_side; }

uint32_t e1000_transmit_kick(void)
{
    if (!e1000_found.controller_ready)
        return 0u;
    e1000_reclaim_transmitted();

    KernelSpscRing_t *const ring = &e1000_transmit_side.ring;
    const uint32_t read_index = ring->consumer.read_index;
    const uint32_t published = atomic_load_acquire(&ring->producer.write_index);
    const uint32_t tail = published - read_index > E1000_TRANSMIT_DESCRIPTORS - 1u ?
                              read_index + (E1000_TRANSMIT_DESCRIPTORS - 1u) :
                              published;
    const uint32_t handed = tail - e1000_transmit_kicked;

    if (handed == 0u)
        return 0u;
    E1000TransmitDescriptor_t *const last = &e1000_transmit_ring[(tail - 1u) & (E1000_TRANSMIT_DESCRIPTORS - 1u)];

    last->status_and_special = 0u;
    last->length_and_command |= E1000_TRANSMIT_REPORT_STATUS << E1000_TRANSMIT_COMMAND_SHIFT;
    e1000_kick_ends[(e1000_kick_first + e1000_kick_count) & (E1000_TRANSMIT_DESCRIPTORS - 1u)] = tail - 1u;
    ++e1000_kick_count;
    e1000_transmit_kicked = tail;
    e1000_counted.frames_sent += handed;
    if (e1000_transmit_tail_held)
    {
        e1000_transmit_tail_behind = true;
        return handed;
    }
    ++e1000_counted.transmit_doorbells;
    e1000_write_register(E1000_REGISTER_TRANSMIT_TAIL, tail & (E1000_TRANSMIT_DESCRIPTORS - 1u));
    return handed;
}

/**
 * @brief The network device's harvest: the receive ring's.
 */
static uint32_t e1000_device_harvest(KernelNetworkDevice_t *device, uint32_t queue)
{
    (void) device;
    (void) queue;
    return e1000_receive_harvest();
}

/**
 * @brief The network device's kick: the transmit ring's.
 */
static uint32_t e1000_device_kick(KernelNetworkDevice_t *device, uint32_t queue)
{
    (void) device;
    (void) queue;
    return e1000_transmit_kick();
}

/**
 * @brief The network device's link: STATUS.LU.
 */
static bool e1000_device_link_up(KernelNetworkDevice_t *device)
{
    (void) device;
    return e1000_link_up();
}

static const KernelNetworkDeviceOperations_t e1000_device_operations = {.harvest = e1000_device_harvest,
                                                                        .kick = e1000_device_kick,
                                                                        .link_up = e1000_device_link_up};
static KernelNetworkQueue_t e1000_device_queue;
static KernelNetworkDevice_t e1000_device;

_Static_assert(sizeof(E1000ReceiveDescriptor_t) % sizeof(uint16_t) == 0u &&
                   sizeof(E1000TransmitDescriptor_t) % sizeof(uint32_t) == 0u,
               "a descriptor's length and command word sit at a whole stride");

KernelNetworkDevice_t *e1000_network_device(void)
{
    if (!e1000_found.controller_ready)
        return NULL;
    e1000_device_queue =
        (KernelNetworkQueue_t){.receive = &e1000_receive_side.ring,
                               .receive_buffers = e1000_receive_buffers,
                               .receive_lengths = &e1000_receive_ring[0].length,
                               .receive_length_stride = sizeof(E1000ReceiveDescriptor_t) / sizeof(uint16_t),
                               .transmit = &e1000_transmit_side.ring,
                               .transmit_buffers = e1000_transmit_buffers,
                               .transmit_words = &e1000_transmit_ring[0].length_and_command,
                               .transmit_word_stride = sizeof(E1000TransmitDescriptor_t) / sizeof(uint32_t),
                               .transmit_command = E1000_TRANSMIT_FRAME_COMMAND};
    e1000_device = (KernelNetworkDevice_t){.name = "e1000",
                                           .buffer_bytes = E1000_BUFFER_BYTES,
                                           .frame_least_bytes = E1000_FRAME_LEAST_BYTES,
                                           .frame_most_bytes = E1000_FRAME_MOST_BYTES,
                                           .queue_count = 1u,
                                           .queues = &e1000_device_queue,
                                           .operations = &e1000_device_operations};
    memcpy(e1000_device.address, e1000_found.address, E1000_ADDRESS_BYTES);
    return &e1000_device;
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
    e1000_write_register(E1000_REGISTER_TRANSMIT_TAIL, e1000_transmit_kicked & (E1000_TRANSMIT_DESCRIPTORS - 1u));
}

bool e1000_debug_read_physical_layer_register(uint32_t physical_layer_register, uint16_t *out_value)
{
    return e1000_found.controller_ready && out_value != NULL && e1000_read_phy(physical_layer_register, out_value);
}

void e1000_debug_forget_loopback(void) { e1000_before_loopback_saved = false; }
#endif
