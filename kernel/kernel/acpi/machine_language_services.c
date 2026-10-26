#include <kernel/acpi/machine_language.h>

#include <string.h>

#define MACHINE_LANGUAGE_OK           ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define MACHINE_LANGUAGE_STATUS(name) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define MACHINE_LANGUAGE_NODE(type)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define MACHINE_LANGUAGE_MAX_TIME     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TIME_REQUESTED
#define MACHINE_LANGUAGE_RING         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NOTIFICATIONS
#define MACHINE_LANGUAGE_SERIALIZED   0x08u /**< MethodFlags bit 3: the method is Serialized. */
#define MACHINE_LANGUAGE_SYNC_LEVEL   0x0Fu /**< The bits of a SyncLevel. */
#define MACHINE_LANGUAGE_LONG_STALL   100u  /**< Microseconds a Stall may last uncounted (§5.5). */
#define MACHINE_LANGUAGE_BUSY_STALL   255u  /**< Microseconds a Stall busy-waits at most; beyond, it sleeps (§5.5). */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t MachineLanguageStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t MachineLanguageLoad_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t MachineLanguageValue_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t MachineLanguageNode_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageServices_t MachineLanguageServices_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageHeldMutex_t MachineLanguageHeldMutex_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNotification_t MachineLanguageNotification_t;
typedef AdvancedConfigurationAndPowerInterfaceHost_t MachineLanguageHost_t;

/**
 * @brief A range of I/O ports, both ends included.
 */
typedef struct MachineLanguagePortRange {
    uint16_t first; /**< Its first port. */
    uint16_t last;  /**< Its last port. */
} MachineLanguagePortRange_t;

/**
 * @brief The strings `\_OSI` answers True, oldest first: a string's position, counted from 1, is the
 *        Windows version it names, in the order Microsoft and ACPICA number them (§5.4).
 */
static const char *const machine_language_windows_strings[] = {
    "Windows 2000",       "Windows 2001",   "Windows 2001 SP1", "Windows 2001.1",   "Windows 2001 SP2",
    "Windows 2001.1 SP1", "Windows 2006",   "Windows 2006.1",   "Windows 2006 SP1", "Windows 2006 SP2",
    "Windows 2009",       "Windows 2012",   "Windows 2013",     "Windows 2015",     "Windows 2016",
    "Windows 2017",       "Windows 2017.2", "Windows 2018",     "Windows 2018.2",   "Windows 2019",
    "Windows 2020",       "Windows 2021",   "Windows 2022",
};

/**
 * @brief The ports Windows' ACPI driver protects once AML asked `\_OSI` for Windows XP or newer, past
 *        those the kernel always protects (§5.3, the table uACPI traced on NT).
 */
static const MachineLanguagePortRange_t machine_language_windows_xp_ports[] = {
    {0x0000u, 0x000Fu}, /* the first DMA controller */
    {0x0040u, 0x0043u}, /* the PIT */
    {0x0048u, 0x004Bu}, /* the second PIT of EISA */
    {0x0070u, 0x0071u}, /* the RTC and CMOS */
    {0x0074u, 0x0076u}, /* the extended CMOS */
    {0x0081u, 0x0083u}, /* DMA page registers */
    {0x0087u, 0x0087u},
    {0x0089u, 0x008Bu},
    {0x008Fu, 0x008Fu},
    {0x0090u, 0x0091u}, /* arbitration control */
    {0x0093u, 0x0094u}, /* system board setup */
    {0x0096u, 0x0097u}, /* POS channel select */
    {0x00C0u, 0x00DFu}, /* the second DMA controller */
};

/**
 * @brief The Windows version a string of @p length bytes names, counted from 1, or 0 for any other.
 */
static uint8_t machine_language_windows_version(const uint8_t *text, uint32_t length)
{
    for (uint32_t index = 0u;
         index < sizeof(machine_language_windows_strings) / sizeof(machine_language_windows_strings[0]); ++index)
    {
        const char *name = machine_language_windows_strings[index];

        if (strlen(name) == length && memcmp(name, text, length) == 0)
            return (uint8_t) (index + 1u);
    }
    return 0u;
}

/**
 * @brief Whether @p port lies in a range of @p ranges.
 */
static bool machine_language_port_in(const MachineLanguagePortRange_t *ranges, uint32_t count, uint16_t port)
{
    for (uint32_t index = 0u; index < count; ++index)
        if (port >= ranges[index].first && port <= ranges[index].last)
            return true;
    return false;
}

/**
 * @brief Charges @p microseconds to the evaluation's budget of requested time (§6.1).
 */
static MachineLanguageStatus_t machine_language_charge_time(MachineLanguageServices_t *services, uint64_t microseconds)
{
    if (microseconds > MACHINE_LANGUAGE_MAX_TIME - services->time_requested)
        return MACHINE_LANGUAGE_STATUS(LIMIT_TIME_REQUESTED);
    services->time_requested += microseconds;
    if (services->time_requested > services->time_requested_peak)
        services->time_requested_peak = services->time_requested;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Whether @p type is an object a Notify may name: a device, a processor or a thermal zone.
 */
static bool machine_language_receives_notifications(uint8_t type)
{
    return type == MACHINE_LANGUAGE_NODE(DEVICE) || type == MACHINE_LANGUAGE_NODE(PROCESSOR) ||
           type == MACHINE_LANGUAGE_NODE(THERMAL_ZONE);
}

/**
 * @brief Whether @p node is `\_GL`, the mutex the global lock stands behind (§5.4).
 */
static bool machine_language_is_global_lock(const MachineLanguageLoad_t *load, uint32_t node)
{
    const MachineLanguageNode_t *mutex =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node);

    return mutex->parent == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT &&
           mutex->name ==
               advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "_GL_", 0u);
}

/**
 * @brief The SyncLevel of the mutex @p node.
 */
static uint8_t machine_language_mutex_level(const MachineLanguageLoad_t *load, uint32_t node)
{
    return (uint8_t) (advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node)
                          ->value.auxiliary &
                      MACHINE_LANGUAGE_SYNC_LEVEL);
}

/**
 * @brief Where the evaluation holds the mutex @p node among its held mutexes, counted from 1, or 0.
 */
static uint32_t machine_language_held_position(const MachineLanguageServices_t *services, uint32_t node)
{
    for (uint32_t position = services->held_count; position != 0u; --position)
        if (services->held[position - 1u].node == node)
            return position;
    return 0u;
}

/**
 * @brief Gives back the mutex held at @p position, counted from 1, its last acquisition released: the
 *        last one acquired gives back the sync level it found; one out of order hands that level to
 *        the mutex acquired after it.
 */
static void machine_language_drop_held(MachineLanguageLoad_t *load, uint32_t position)
{
    MachineLanguageServices_t *services = &load->services;
    const MachineLanguageHeldMutex_t held = services->held[position - 1u];

    if (machine_language_is_global_lock(load, held.node))
        advanced_configuration_and_power_interface_machine_language_release_global_lock(load);
    if (position == services->held_count)
        services->sync_level = held.level_before;
    else
    {
        services->held[position].level_before = held.level_before;
        memmove(&services->held[position - 1u], &services->held[position],
                (services->held_count - position) * sizeof(services->held[0]));
    }
    --services->held_count;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_operating_system_interface(
    MachineLanguageLoad_t *load, const MachineLanguageValue_t *argument, bool *out_supported)
{
    MachineLanguageServices_t *services = &load->services;

    *out_supported = false;
    if (argument->kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *string =
        advanced_configuration_and_power_interface_machine_language_object(&load->heap, argument->handle);
    const uint8_t version = machine_language_windows_version((const uint8_t *) (string + 1), string->length);

    ++services->operating_system_queries;
    services->operating_system_queries_denied += (version == 0u) ? 1u : 0u;
    if (version > services->windows_version)
        services->windows_version = version;
    *out_supported = version != 0u;
    return MACHINE_LANGUAGE_OK;
}

bool advanced_configuration_and_power_interface_machine_language_port_is_protected(const MachineLanguageLoad_t *load,
                                                                                   uint16_t port)
{
    if (advanced_configuration_and_power_interface_port_is_protected(port))
        return true;
    return load->services.windows_version >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_WINDOWS_XP &&
           machine_language_port_in(
               machine_language_windows_xp_ports,
               sizeof(machine_language_windows_xp_ports) / sizeof(machine_language_windows_xp_ports[0]), port);
}

void advanced_configuration_and_power_interface_machine_language_begin_services(MachineLanguageLoad_t *load)
{
    load->services.time_requested = 0u;
}

void advanced_configuration_and_power_interface_machine_language_end_services(MachineLanguageLoad_t *load)
{
    MachineLanguageServices_t *services = &load->services;

    services->released_at_end += services->held_count;
    while (services->held_count != 0u)
        machine_language_drop_held(load, services->held_count);
    services->sync_level = 0u;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_sleep(MachineLanguageLoad_t *load,
                                                                                          uint64_t milliseconds)
{
    MachineLanguageServices_t *services = &load->services;
    const MachineLanguageHost_t *host = load->space.host;
    const MachineLanguageStatus_t status = (milliseconds > MACHINE_LANGUAGE_MAX_TIME / 1000u) ?
                                               MACHINE_LANGUAGE_STATUS(LIMIT_TIME_REQUESTED) :
                                               machine_language_charge_time(services, milliseconds * 1000u);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    ++services->sleeps;
    services->sleep_milliseconds += milliseconds;
    if (milliseconds != 0u)
        services->waited_milliseconds += host->sleep_milliseconds(host->context, (uint32_t) milliseconds);
    return MACHINE_LANGUAGE_OK;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_stall(MachineLanguageLoad_t *load,
                                                                                          uint64_t microseconds)
{
    MachineLanguageServices_t *services = &load->services;
    const MachineLanguageHost_t *host = load->space.host;
    const MachineLanguageStatus_t status = machine_language_charge_time(services, microseconds);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    ++services->stalls;
    services->stall_microseconds += microseconds;
    services->long_stalls += (microseconds > MACHINE_LANGUAGE_LONG_STALL) ? 1u : 0u;
    if (microseconds <= MACHINE_LANGUAGE_BUSY_STALL)
    {
        if (microseconds != 0u)
            services->stall_waited_microseconds += host->stall_microseconds(host->context, (uint32_t) microseconds);
        return MACHINE_LANGUAGE_OK;
    }
    ++services->stalls_slept;
    services->waited_milliseconds +=
        host->sleep_milliseconds(host->context, (uint32_t) ((microseconds + 999u) / 1000u));
    return MACHINE_LANGUAGE_OK;
}

uint64_t advanced_configuration_and_power_interface_machine_language_timer(MachineLanguageLoad_t *load)
{
    const MachineLanguageHost_t *host = load->space.host;
    const uint64_t clock = host->clock_100ns(host->context);

    ++load->services.timer_reads;
    return (load->integer_bits == 32u) ? (clock & 0xFFFFFFFFu) : clock;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_notify(MachineLanguageLoad_t *load,
                                                                                           uint32_t node,
                                                                                           uint64_t value)
{
    MachineLanguageServices_t *services = &load->services;

    if (!machine_language_receives_notifications(
            advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node)->type))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    ++services->notifications;
    if (services->notification_count == MACHINE_LANGUAGE_RING)
    {
        ++services->notifications_dropped;
        return MACHINE_LANGUAGE_OK;
    }

    MachineLanguageNotification_t *slot =
        &services->ring[(services->notification_first + services->notification_count) % MACHINE_LANGUAGE_RING];

    slot->node = node;
    slot->value = (uint32_t) value;
    if (++services->notification_count > services->notification_peak)
        services->notification_peak = services->notification_count;
    return MACHINE_LANGUAGE_OK;
}

bool advanced_configuration_and_power_interface_machine_language_take_notification(
    MachineLanguageLoad_t *load, MachineLanguageNotification_t *out_notification)
{
    MachineLanguageServices_t *services = &load->services;

    if (services->notification_count == 0u)
        return false;
    *out_notification = services->ring[services->notification_first];
    services->notification_first = (services->notification_first + 1u) % MACHINE_LANGUAGE_RING;
    --services->notification_count;
    ++services->notifications_taken;
    return true;
}

void advanced_configuration_and_power_interface_machine_language_drop_notifications(MachineLanguageLoad_t *load)
{
    MachineLanguageNotification_t notification;

    while (advanced_configuration_and_power_interface_machine_language_take_notification(load, &notification))
        continue;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_acquire(MachineLanguageLoad_t *load,
                                                                                            uint32_t node,
                                                                                            bool *out_acquired)
{
    MachineLanguageServices_t *services = &load->services;

    *out_acquired = false;
    if (advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node)->type !=
        MACHINE_LANGUAGE_NODE(MUTEX))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

    const uint8_t level = machine_language_mutex_level(load, node);
    const uint32_t position = machine_language_held_position(services, node);

    if (level < services->sync_level)
    {
        ++services->acquisitions_ignored;
        return MACHINE_LANGUAGE_OK;
    }
    if (position != 0u)
        ++services->held[position - 1u].depth;
    else
    {
        if (services->held_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_HELD_MUTEXES)
            return MACHINE_LANGUAGE_STATUS(LIMIT_HELD_MUTEXES);

        const MachineLanguageStatus_t status =
            machine_language_is_global_lock(load, node) ?
                advanced_configuration_and_power_interface_machine_language_take_global_lock(load) :
                MACHINE_LANGUAGE_OK;

        if (status != MACHINE_LANGUAGE_OK)
            return status;
        services->held[services->held_count].node = node;
        services->held[services->held_count].depth = 1u;
        services->held[services->held_count].level_before = services->sync_level;
        if (++services->held_count > services->held_peak)
            services->held_peak = services->held_count;
        services->sync_level = level;
    }
    ++services->acquisitions;
    *out_acquired = true;
    return MACHINE_LANGUAGE_OK;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_release(MachineLanguageLoad_t *load,
                                                                                            uint32_t node)
{
    MachineLanguageServices_t *services = &load->services;

    if (advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node)->type !=
        MACHINE_LANGUAGE_NODE(MUTEX))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

    const uint32_t position = machine_language_held_position(services, node);

    if (position == 0u || machine_language_mutex_level(load, node) !=
                              machine_language_mutex_level(load, services->held[services->held_count - 1u].node))
    {
        ++services->releases_ignored;
        return MACHINE_LANGUAGE_OK;
    }
    if (--services->held[position - 1u].depth == 0u)
        machine_language_drop_held(load, position);
    return MACHINE_LANGUAGE_OK;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_signal(MachineLanguageLoad_t *load,
                                                                                           uint32_t node)
{
    MachineLanguageNode_t *event =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node);

    if (event->type != MACHINE_LANGUAGE_NODE(EVENT))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (event->value.payload < UINT32_MAX)
        ++event->value.payload;
    ++load->services.signals;
    return MACHINE_LANGUAGE_OK;
}

MachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_enter_method(MachineLanguageLoad_t *load, uint8_t flags,
                                                                         uint8_t *out_saved)
{
    MachineLanguageServices_t *services = &load->services;
    const uint8_t level = (uint8_t) (flags >> 4u);

    *out_saved = 0u;
    if ((flags & MACHINE_LANGUAGE_SERIALIZED) == 0u)
        return MACHINE_LANGUAGE_OK;
    if (level < services->sync_level)
        return MACHINE_LANGUAGE_STATUS(MUTEX_ORDER);
    *out_saved = (uint8_t) (services->sync_level + 1u);
    services->sync_level = level;
    return MACHINE_LANGUAGE_OK;
}

void advanced_configuration_and_power_interface_machine_language_leave_method(MachineLanguageLoad_t *load,
                                                                              uint8_t saved)
{
    if (saved != 0u)
        load->services.sync_level = (uint8_t) (saved - 1u);
}
