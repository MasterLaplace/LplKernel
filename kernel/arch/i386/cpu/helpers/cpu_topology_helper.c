#include <kernel/config.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/helpers/cpu_topology_helper.h>

void write_cpu_topology_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: cpu topology slot=");
    serial_write_int(serial, (int32_t) cpu_topology_get_logical_slot());
    serial_write_string(serial, ", apic_id=");
    serial_write_int(serial, (int32_t) cpu_topology_get_local_apic_id());
    serial_write_string(serial, ", cpus=");
    serial_write_int(serial, (int32_t) cpu_topology_get_discovered_cpu_count());
    serial_write_string(serial, ", online=");
    serial_write_int(serial, (int32_t) cpu_topology_get_online_cpu_count());
    serial_write_string(serial, ", source=");
    serial_write_string(serial, cpu_topology_get_source_name());
    serial_write_string(serial, ", forced=");
    serial_write_int(serial, (int32_t) cpu_topology_is_forced());
    serial_write_string(serial, "\n");
}

void write_cpu_topology_madt_sync_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: cpu topology MADT sync cpus=");
    serial_write_int(serial, (int32_t) cpu_topology_get_discovered_cpu_count());
    serial_write_string(serial, "\n");
}
