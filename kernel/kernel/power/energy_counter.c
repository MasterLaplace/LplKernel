#include <kernel/power/energy_counter.h>

#include <kernel/cpu/apic_timer.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/tickless.h>

#include <string.h>

#define ENERGY_COUNTER_FAMILY_6                  6u          /**< The only Intel family whose models the table lists. */
#define ENERGY_COUNTER_RAPL_BIT                  (1u << 14)  /**< CPUID 0x80000007 EDX bit 14: AMD's RAPL interface. */
#define ENERGY_COUNTER_EXTENDED_LEAVES           0x80000000u /**< The CPUID leaf that gives the highest extended one. */
#define ENERGY_COUNTER_EXTENDED_POWER_LEAF       0x80000007u /**< The CPUID leaf of the RAPL bit. */
#define ENERGY_COUNTER_ENERGY_UNIT_SHIFT         8u          /**< ESU, bits 12:8 of the unit register. */
#define ENERGY_COUNTER_ENERGY_UNIT_MASK          0x1Fu       /**< Its width. */
#define ENERGY_COUNTER_POWER_UNIT_MASK           0x0Fu       /**< PU, bits 3:0 of the unit register. */
#define ENERGY_COUNTER_POWER_FIELD_MASK          0x7FFFu /**< A power field, bits 14:0 of the power info or limit. */
#define ENERGY_COUNTER_POWER_LIMIT_ENABLE        0x8000u /**< Power limit 1's enable, bit 15 of the power limit. */
#define ENERGY_COUNTER_NANOJOULES_PER_JOULE      1000000000ull /**< Nanojoules in a joule. */
#define ENERGY_COUNTER_NANOJOULES_PER_MICROJOULE 1000ull       /**< Nanojoules in a microjoule. */
#define ENERGY_COUNTER_MILLIWATTS_PER_WATT       1000ull       /**< Milliwatts in a watt. */

/**
 * @brief An Intel model of Linux v6.12's `rapl_ids` (drivers/powercap/intel_rapl_common.c), its family 6 implied.
 */
typedef struct {
    uint8_t model; /**< The displayed model. */
    bool atom;     /**< Whether its units are an Atom's (`rapl_check_unit_atom`). */
} EnergyCounterIntelModel_t;

static const EnergyCounterIntelModel_t energy_counter_intel_models[] = {
    {0x2Au, false},
    {0x2Du, false},
    {0x3Au, false},
    {0x3Eu, false},
    {0x3Cu, false},
    {0x45u, false},
    {0x46u, false},
    {0x3Fu, false},
    {0x3Du, false},
    {0x47u, false},
    {0x56u, false},
    {0x4Fu, false},
    {0x5Eu, false},
    {0x4Eu, false},
    {0x55u, false},
    {0x8Eu, false},
    {0x9Eu, false},
    {0x66u, false},
    {0x7Eu, false},
    {0x7Du, false},
    {0x9Du, false},
    {0x6Au, false},
    {0x6Cu, false},
    {0xA6u, false},
    {0xA5u, false},
    {0x8Cu, false},
    {0x8Du, false},
    {0xA7u, false},
    {0x97u, false},
    {0x9Au, false},
    {0xBEu, false},
    {0xB7u, false},
    {0xBAu, false},
    {0xBFu, false},
    {0xACu, false},
    {0xAAu, false},
    {0x8Fu, false},
    {0xCFu, false},
    {0xBDu, false},
    {0xC5u, false},
    {0xC6u, false},
    {0xB5u, false},
    {0x8Au, false},
    {0x37u, true },
    {0x4Cu, true },
    {0x4Au, true },
    {0x5Au, true },
    {0x5Cu, false},
    {0x7Au, false},
    {0x5Fu, false},
    {0x96u, false},
    {0x86u, false},
    {0x9Cu, false},
    {0x57u, false},
    {0x85u, false},
};

_Static_assert(sizeof(energy_counter_intel_models) / sizeof(energy_counter_intel_models[0]) == 55u,
               "the 55 Intel models of Linux v6.12's rapl_ids");

static KernelEnergyCounter_t energy_counter_found = {.state = KERNEL_ENERGY_COUNTER_NOT_DETECTED};

/**
 * @brief Whether the processor's vendor string is @p vendor, of CPU_IDENTITY_SIGNATURE_SIZE bytes with its terminator.
 */
static bool energy_counter_vendor_is(const CpuIdentity_t *identity, const char *vendor)
{
    return memcmp(identity->vendor, vendor, CPU_IDENTITY_SIGNATURE_SIZE) == 0;
}

/**
 * @brief The backend of an Intel processor of family 6 and @p model: its row of the table, none without one.
 */
static uint32_t energy_counter_intel_backend(uint32_t model)
{
    for (uint32_t index = 0u; index < sizeof(energy_counter_intel_models) / sizeof(energy_counter_intel_models[0]);
         ++index)
        if (energy_counter_intel_models[index].model == model)
            return energy_counter_intel_models[index].atom ? KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM :
                                                             KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE;
    return KERNEL_ENERGY_COUNTER_BACKEND_NONE;
}

/**
 * @brief The unit register of @p backend.
 */
static uint32_t energy_counter_unit_register(uint32_t backend)
{
    return backend == KERNEL_ENERGY_COUNTER_BACKEND_AMD ? KERNEL_ENERGY_COUNTER_AMD_UNIT_REGISTER :
                                                          KERNEL_ENERGY_COUNTER_INTEL_UNIT_REGISTER;
}

/**
 * @brief The package counter of @p backend.
 */
static uint32_t energy_counter_package_register(uint32_t backend)
{
    return backend == KERNEL_ENERGY_COUNTER_BACKEND_AMD ? KERNEL_ENERGY_COUNTER_AMD_PACKAGE_REGISTER :
                                                          KERNEL_ENERGY_COUNTER_INTEL_PACKAGE_REGISTER;
}

/**
 * @brief Reads the power field of @p msr into @p out_milliwatts, and says whether it read.
 */
static bool energy_counter_read_power_field(const KernelEnergyCounter_t *found, KernelEnergyCounterReader_t read,
                                            void *context, uint32_t msr, uint32_t *out_milliwatts)
{
    uint64_t value = 0u;

    if (!read(context, msr, &value))
        return false;
    *out_milliwatts = kernel_energy_counter_milliwatts(found->backend, found->power_unit_exponent,
                                                       (uint32_t) value & ENERGY_COUNTER_POWER_FIELD_MASK);
    return true;
}

/**
 * @brief Reads Intel's power info and power limit, each known where it reads.
 */
static void energy_counter_read_intel_power(KernelEnergyCounter_t *found, KernelEnergyCounterReader_t read,
                                            void *context)
{
    found->thermal_design_power_known = energy_counter_read_power_field(
        found, read, context, KERNEL_ENERGY_COUNTER_INTEL_POWER_INFO_REGISTER, &found->thermal_design_power_milliwatts);
    uint64_t limit = 0u;

    found->power_limit_known = read(context, KERNEL_ENERGY_COUNTER_INTEL_POWER_LIMIT_REGISTER, &limit);
    if (!found->power_limit_known)
        return;
    found->power_limit_milliwatts = kernel_energy_counter_milliwatts(
        found->backend, found->power_unit_exponent, (uint32_t) limit & ENERGY_COUNTER_POWER_FIELD_MASK);
    found->power_limit_enabled = ((uint32_t) limit & ENERGY_COUNTER_POWER_LIMIT_ENABLE) != 0u;
}

/**
 * @brief Whether the package counter reads twice across the settling wait and moves; the state that says why not.
 */
static uint32_t energy_counter_settle(const KernelEnergyCounter_t *found, KernelEnergyCounterReader_t read,
                                      KernelEnergyCounterWait_t wait, void *context)
{
    uint64_t before = 0u;
    uint64_t after = 0u;

    if (!read(context, found->package_register, &before))
        return KERNEL_ENERGY_COUNTER_COUNTER_FAULTED;
    if (!wait(context, KERNEL_ENERGY_COUNTER_SETTLE_MICROSECONDS))
        return KERNEL_ENERGY_COUNTER_UNTIMED;
    if (!read(context, found->package_register, &after))
        return KERNEL_ENERGY_COUNTER_COUNTER_FAULTED;
    return (uint32_t) before == (uint32_t) after ? KERNEL_ENERGY_COUNTER_FROZEN : KERNEL_ENERGY_COUNTER_PRESENT;
}

/**
 * @brief The detection's reader: the fault-tolerant RDMSR.
 */
static bool energy_counter_read_machine(void *context, uint32_t msr, uint64_t *out_value)
{
    (void) context;
    return asmutils_read_model_specific_register_or_refuse(msr, out_value);
}

/**
 * @brief The detection's wait: the calibrated TSC, spun on; false without a calibration.
 */
static bool energy_counter_wait_machine(void *context, uint32_t microseconds)
{
    const uint64_t hertz = advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz();

    (void) context;
    if (hertz == 0u)
        return false;

    const uint64_t until = asmutils_read_timestamp_counter() + kernel_tickless_cycles_from_microseconds(microseconds);
    while (asmutils_read_timestamp_counter() < until)
        asmutils_pause();
    return true;
}

/**
 * @brief CPUID 0x80000007 EDX of the processor that runs the call, 0 where the leaf does not exist.
 */
static uint32_t energy_counter_extended_power_edx(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(ENERGY_COUNTER_EXTENDED_LEAVES, 0u, &eax, &ebx, &ecx, &edx);
    if (eax < ENERGY_COUNTER_EXTENDED_POWER_LEAF)
        return 0u;
    asmutils_cpuid(ENERGY_COUNTER_EXTENDED_POWER_LEAF, 0u, &eax, &ebx, &ecx, &edx);
    return edx;
}

uint32_t kernel_energy_counter_backend_of(const CpuIdentity_t *identity, uint32_t extended_power_edx)
{
    if (energy_counter_vendor_is(identity, "GenuineIntel"))
        return identity->family == ENERGY_COUNTER_FAMILY_6 ? energy_counter_intel_backend(identity->model) :
                                                             KERNEL_ENERGY_COUNTER_BACKEND_NONE;
    if ((energy_counter_vendor_is(identity, "AuthenticAMD") || energy_counter_vendor_is(identity, "HygonGenuine")) &&
        (extended_power_edx & ENERGY_COUNTER_RAPL_BIT) != 0u)
        return KERNEL_ENERGY_COUNTER_BACKEND_AMD;
    return KERNEL_ENERGY_COUNTER_BACKEND_NONE;
}

uint64_t kernel_energy_counter_nanojoules(uint32_t backend, uint32_t energy_unit_exponent, uint32_t counts)
{
    if (backend == KERNEL_ENERGY_COUNTER_BACKEND_NONE)
        return 0u;
    if (backend != KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM)
        return ((uint64_t) counts * ENERGY_COUNTER_NANOJOULES_PER_JOULE) >> energy_unit_exponent;

    const uint64_t unit = ENERGY_COUNTER_NANOJOULES_PER_MICROJOULE << energy_unit_exponent;
    return counts != 0u && unit > UINT64_MAX / counts ? UINT64_MAX : unit * counts;
}

uint32_t kernel_energy_counter_counts_between(uint32_t before, uint32_t after) { return after - before; }

uint32_t kernel_energy_counter_milliwatts(uint32_t backend, uint32_t power_unit_exponent, uint32_t raw)
{
    if (backend == KERNEL_ENERGY_COUNTER_BACKEND_NONE)
        return 0u;

    const uint64_t milliwatts = backend == KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM ?
                                    (uint64_t) raw << power_unit_exponent :
                                    ((uint64_t) raw * ENERGY_COUNTER_MILLIWATTS_PER_WATT) >> power_unit_exponent;
    return milliwatts > UINT32_MAX ? UINT32_MAX : (uint32_t) milliwatts;
}

void kernel_energy_counter_detect(uint32_t backend, KernelEnergyCounterReader_t read, KernelEnergyCounterWait_t wait,
                                  void *context, KernelEnergyCounter_t *out)
{
    uint64_t units = 0u;

    memset(out, 0, sizeof(*out));
    out->backend = backend;
    out->state = KERNEL_ENERGY_COUNTER_NOT_LISTED;
    if (backend == KERNEL_ENERGY_COUNTER_BACKEND_NONE)
        return;
    out->package_register = energy_counter_package_register(backend);
    out->state = KERNEL_ENERGY_COUNTER_UNIT_FAULTED;
    if (!read(context, energy_counter_unit_register(backend), &units))
        return;
    out->energy_unit_exponent =
        (uint32_t) (units >> ENERGY_COUNTER_ENERGY_UNIT_SHIFT) & ENERGY_COUNTER_ENERGY_UNIT_MASK;
    out->power_unit_exponent = (uint32_t) units & ENERGY_COUNTER_POWER_UNIT_MASK;
    out->state = KERNEL_ENERGY_COUNTER_NO_UNIT;
    if (out->energy_unit_exponent == 0u && backend != KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM)
        return;
    out->state = energy_counter_settle(out, read, wait, context);
    if (out->state == KERNEL_ENERGY_COUNTER_PRESENT && backend != KERNEL_ENERGY_COUNTER_BACKEND_AMD)
        energy_counter_read_intel_power(out, read, context);
}

void kernel_energy_counter_initialize(void)
{
    CpuIdentity_t identity;

    cpu_identity_read(&identity);
    kernel_energy_counter_detect(kernel_energy_counter_backend_of(&identity, energy_counter_extended_power_edx()),
                                 energy_counter_read_machine, energy_counter_wait_machine, NULL, &energy_counter_found);
}

const KernelEnergyCounter_t *kernel_energy_counter(void) { return &energy_counter_found; }

bool kernel_energy_counter_sample(uint32_t *out_counts)
{
    uint64_t value = 0u;

    if (energy_counter_found.state != KERNEL_ENERGY_COUNTER_PRESENT ||
        !asmutils_read_model_specific_register_or_refuse(energy_counter_found.package_register, &value))
        return false;
    *out_counts = (uint32_t) value;
    return true;
}

uint64_t kernel_energy_counter_nanojoules_between_of(const KernelEnergyCounter_t *found, uint32_t before,
                                                     uint32_t after)
{
    if (found->state != KERNEL_ENERGY_COUNTER_PRESENT)
        return 0u;
    return kernel_energy_counter_nanojoules(found->backend, found->energy_unit_exponent,
                                            kernel_energy_counter_counts_between(before, after));
}

uint64_t kernel_energy_counter_nanojoules_between(uint32_t before, uint32_t after)
{
    return kernel_energy_counter_nanojoules_between_of(&energy_counter_found, before, after);
}

const char *kernel_energy_counter_backend_name(uint32_t backend)
{
    static const char *const names[] = {"none", "intel_core", "intel_atom", "amd"};

    return backend < sizeof(names) / sizeof(names[0]) ? names[backend] : "unknown";
}

const char *kernel_energy_counter_state_name(uint32_t state)
{
    static const char *const names[] = {"present", "not_detected", "not_listed", "unit_faulted",
                                        "no_unit", "frozen",       "untimed",    "counter_faulted"};

    return state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}
