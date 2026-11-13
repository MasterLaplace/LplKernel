#include <kernel/cpu/cpu_identity.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/energy_counter.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(energy_counter, KERNEL_TEST_STAGE_BOOTED);

/** CPUID 0x80000007 EDX bit 14: AMD's RAPL interface. */
#define ENERGY_COUNTER_TEST_RAPL_BIT (1u << 14)
/** An Intel unit register as Skylake gives it: PU 3, ESU 14, time unit 10. */
#define ENERGY_COUNTER_TEST_SKYLAKE_UNITS 0x000A0E03ull
/** The registers a made-up reader answers, at most. */
#define ENERGY_COUNTER_TEST_REGISTERS 4u

/**
 * @brief A processor identity of @p vendor and of family @p family and model @p model.
 */
static CpuIdentity_t energy_counter_test_identity(const char *vendor, uint32_t family, uint32_t model)
{
    CpuIdentity_t identity;

    memset(&identity, 0, sizeof(identity));
    memcpy(identity.vendor, vendor, CPU_IDENTITY_SIGNATURE_SIZE);
    identity.family = family;
    identity.model = model;
    return identity;
}

/**
 * @brief The backend of a processor of @p vendor, @p family and @p model, CPUID 0x80000007 EDX @p edx.
 */
static uint32_t energy_counter_test_backend(const char *vendor, uint32_t family, uint32_t model, uint32_t edx)
{
    const CpuIdentity_t identity = energy_counter_test_identity(vendor, family, model);

    return kernel_energy_counter_backend_of(&identity, edx);
}

/**
 * @brief Intel's counter is read on the models of Linux's table, the Silvermont and Airmont Atoms in Atom units, and on
 *        no other; AMD's and Hygon's where CPUID sets the RAPL bit, whatever their family.
 */
KERNEL_TEST(the_backend_is_intel_s_on_listed_models_and_amd_s_where_cpuid_says_so)
{
    kernel_test_check(
        test, energy_counter_test_backend("GenuineIntel", 6u, 0x2Au, 0u) == KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE,
        "Sandy Bridge, the first listed, is a core");
    kernel_test_check(
        test, energy_counter_test_backend("GenuineIntel", 6u, 0xAAu, 0u) == KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE,
        "Meteor Lake, the bench's, is one");
    kernel_test_check(
        test, energy_counter_test_backend("GenuineIntel", 6u, 0x85u, 0u) == KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE,
        "Knights Mill, the last listed, is one");
    kernel_test_check(
        test, energy_counter_test_backend("GenuineIntel", 6u, 0x37u, 0u) == KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM,
        "Silvermont counts in Atom units");
    kernel_test_check(
        test, energy_counter_test_backend("GenuineIntel", 6u, 0x5Au, 0u) == KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM,
        "and Moorefield's Airmont");
    kernel_test_check(
        test, energy_counter_test_backend("GenuineIntel", 6u, 0x5Cu, 0u) == KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE,
        "Goldmont counts as a core");
    kernel_test_check(test,
                      energy_counter_test_backend("GenuineIntel", 6u, 0x2Bu, 0u) == KERNEL_ENERGY_COUNTER_BACKEND_NONE,
                      "a model the table does not list is none");
    kernel_test_check(
        test, energy_counter_test_backend("GenuineIntel", 0x13u, 0x2Au, 0u) == KERNEL_ENERGY_COUNTER_BACKEND_NONE,
        "nor a listed model of another family");
    kernel_test_check(test,
                      energy_counter_test_backend("AuthenticAMD", 0x19u, 0x61u, ENERGY_COUNTER_TEST_RAPL_BIT) ==
                          KERNEL_ENERGY_COUNTER_BACKEND_AMD,
                      "AMD with the RAPL bit is AMD's");
    kernel_test_check(test,
                      energy_counter_test_backend("AuthenticAMD", 0x19u, 0x61u, ~ENERGY_COUNTER_TEST_RAPL_BIT) ==
                          KERNEL_ENERGY_COUNTER_BACKEND_NONE,
                      "without it, none, whatever else the leaf sets");
    kernel_test_check(test,
                      energy_counter_test_backend("HygonGenuine", 0x18u, 0u, ENERGY_COUNTER_TEST_RAPL_BIT) ==
                          KERNEL_ENERGY_COUNTER_BACKEND_AMD,
                      "Hygon with the bit is AMD's");
    kernel_test_check(test,
                      energy_counter_test_backend("GenuineIntel", 6u, 0x2Bu, ENERGY_COUNTER_TEST_RAPL_BIT) ==
                          KERNEL_ENERGY_COUNTER_BACKEND_NONE,
                      "Intel is never told by the AMD bit");
    kernel_test_check(test,
                      energy_counter_test_backend("CentaurHauls", 6u, 0x2Au, ENERGY_COUNTER_TEST_RAPL_BIT) ==
                          KERNEL_ENERGY_COUNTER_BACKEND_NONE,
                      "and another vendor is none");
}

/**
 * @brief Energy units become nanojoules as the SDM's 1/2^ESU J, rounded down, without overflow on a whole counter, and
 *        as 2^ESU uJ on an Atom; no backend gives none.
 */
KERNEL_TEST(counts_become_nanojoules_in_the_backend_s_units)
{
    const uint32_t core = KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE;

    kernel_test_check(test, kernel_energy_counter_nanojoules(core, 14u, 1u) == 61035u, "ESU 14: 61035.15 nJ, down");
    kernel_test_check(test, kernel_energy_counter_nanojoules(core, 14u, 16384u) == 1000000000u, "2^14 of them: 1 J");
    kernel_test_check(test,
                      kernel_energy_counter_nanojoules(KERNEL_ENERGY_COUNTER_BACKEND_AMD, 16u, 65536u) == 1000000000u,
                      "AMD's ESU 16: 2^16 units, 1 J");
    kernel_test_check(test, kernel_energy_counter_nanojoules(core, 14u, 0xFFFFFFFFu) == 262143999938964ull,
                      "a whole counter, 2^32 - 1 units, without overflow");
    kernel_test_check(test, kernel_energy_counter_nanojoules(core, 0u, 3u) == 3000000000ull, "ESU 0: joules");
    kernel_test_check(test,
                      kernel_energy_counter_nanojoules(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM, 5u, 1u) == 32000u,
                      "an Atom's ESU 5: 32 uJ");
    kernel_test_check(test,
                      kernel_energy_counter_nanojoules(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM, 31u, 0xFFFFFFFFu) ==
                          UINT64_MAX,
                      "past 64 bits, the largest value");
    kernel_test_check(test, kernel_energy_counter_nanojoules(KERNEL_ENERGY_COUNTER_BACKEND_NONE, 14u, 1u) == 0u,
                      "no backend, none");
}

/**
 * @brief The counts between two reads are their difference modulo 2^32, so that one wrap is counted right.
 */
KERNEL_TEST(the_counts_between_two_reads_survive_one_wrap)
{
    kernel_test_check(test, kernel_energy_counter_counts_between(100u, 250u) == 150u, "no wrap");
    kernel_test_check(test, kernel_energy_counter_counts_between(0xFFFFFFF0u, 0x10u) == 0x20u, "one wrap");
    kernel_test_check(test, kernel_energy_counter_counts_between(7u, 7u) == 0u, "no move");
}

/**
 * @brief Power fields become milliwatts as 1/2^PU W, or as 2^PU mW on an Atom.
 */
KERNEL_TEST(power_fields_become_milliwatts_in_the_backend_s_units)
{
    kernel_test_check(test,
                      kernel_energy_counter_milliwatts(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE, 3u, 120u) == 15000u,
                      "PU 3, 120 units: 15 W");
    kernel_test_check(test, kernel_energy_counter_milliwatts(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM, 5u, 10u) == 320u,
                      "an Atom's PU 5, 10 units: 320 mW");
    kernel_test_check(test,
                      kernel_energy_counter_milliwatts(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM, 15u, 0x7FFFu) ==
                          0x7FFFu << 15u,
                      "a whole 15-bit field at PU 15 fits in 32 bits");
    kernel_test_check(
        test, kernel_energy_counter_milliwatts(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_ATOM, 15u, 0x20000u) == UINT32_MAX,
        "a raw past its 15 bits, past 32 bits: the largest value");
    kernel_test_check(test, kernel_energy_counter_milliwatts(KERNEL_ENERGY_COUNTER_BACKEND_NONE, 3u, 120u) == 0u,
                      "no backend, none");
}

/**
 * @brief A made-up processor: the registers it answers, the package counter's successive values, and whether the
 *        clock can time a wait.
 */
typedef struct {
    uint32_t msrs[ENERGY_COUNTER_TEST_REGISTERS];   /**< The registers it gives. */
    uint64_t values[ENERGY_COUNTER_TEST_REGISTERS]; /**< Their values. */
    uint32_t count;                                 /**< How many. */
    uint32_t package_msr;                           /**< The package counter, which moves after each wait. */
    uint32_t package_step;                          /**< By how much. */
    uint32_t package_value;                         /**< Its value now. */
    bool timed;                                     /**< Whether a wait can be timed. */
    uint32_t waits;                                 /**< Waits asked. */
} EnergyCounterTestMachine_t;

static bool energy_counter_test_read(void *context, uint32_t msr, uint64_t *out_value)
{
    const EnergyCounterTestMachine_t *machine = (const EnergyCounterTestMachine_t *) context;

    if (msr == machine->package_msr && machine->package_msr != 0u)
    {
        *out_value = machine->package_value;
        return true;
    }
    for (uint32_t index = 0u; index < machine->count; ++index)
        if (machine->msrs[index] == msr)
        {
            *out_value = machine->values[index];
            return true;
        }
    return false;
}

static bool energy_counter_test_wait(void *context, uint32_t microseconds)
{
    EnergyCounterTestMachine_t *machine = (EnergyCounterTestMachine_t *) context;

    (void) microseconds;
    ++machine->waits;
    machine->package_value += machine->package_step;
    return machine->timed;
}

/**
 * @brief A made-up Skylake: units PU 3 and ESU 14, a counter that moves, a TDP of 120 units and a power limit of 200.
 */
static EnergyCounterTestMachine_t energy_counter_test_skylake(void)
{
    EnergyCounterTestMachine_t machine;

    memset(&machine, 0, sizeof(machine));
    machine.msrs[0] = KERNEL_ENERGY_COUNTER_INTEL_UNIT_MSR;
    machine.values[0] = ENERGY_COUNTER_TEST_SKYLAKE_UNITS;
    machine.msrs[1] = KERNEL_ENERGY_COUNTER_INTEL_POWER_INFO_MSR;
    machine.values[1] = 0x12340078u;
    machine.msrs[2] = KERNEL_ENERGY_COUNTER_INTEL_POWER_LIMIT_MSR;
    machine.values[2] = 0x000080C8u;
    machine.count = 3u;
    machine.package_msr = KERNEL_ENERGY_COUNTER_INTEL_PACKAGE_MSR;
    machine.package_step = 33u;
    machine.package_value = 0xFFFFFFF0u;
    machine.timed = true;
    return machine;
}

/**
 * @brief A counter whose unit register reads, gives a unit, and moves across the wait is present, with its units, and,
 *        on Intel, the TDP and the power limit in milliwatts; a power field that faults is unknown, the counter still
 *        present.
 */
KERNEL_TEST(a_counter_that_gives_a_unit_and_moves_is_present)
{
    static KernelEnergyCounter_t found;
    EnergyCounterTestMachine_t machine = energy_counter_test_skylake();

    kernel_energy_counter_detect(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE, energy_counter_test_read,
                                 energy_counter_test_wait, &machine, &found);
    kernel_test_check(test, found.state == KERNEL_ENERGY_COUNTER_PRESENT, "present");
    kernel_test_check(test,
                      found.energy_unit_exponent == 14u && found.power_unit_exponent == 3u &&
                          found.package_msr == KERNEL_ENERGY_COUNTER_INTEL_PACKAGE_MSR,
                      "ESU 14, PU 3, Intel's package counter");
    kernel_test_check(test, found.thermal_design_power_known && found.thermal_design_power_milliwatts == 15000u,
                      "a TDP of 120 units is 15 W");
    kernel_test_check(test, found.power_limit_known && found.power_limit_milliwatts == 25000u,
                      "a power limit of 200 units is 25 W");
    kernel_test_check(test, machine.waits == 1u, "after one settling wait");

    machine = energy_counter_test_skylake();
    machine.count = 1u;
    kernel_energy_counter_detect(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE, energy_counter_test_read,
                                 energy_counter_test_wait, &machine, &found);
    kernel_test_check(test,
                      found.state == KERNEL_ENERGY_COUNTER_PRESENT && !found.thermal_design_power_known &&
                          !found.power_limit_known,
                      "power fields that fault are unknown, the counter present all the same");
}

/**
 * @brief A counter is absent, by its reason: no backend; a unit register that faults; one that gives ESU 0, as KVM's
 *        reads 0; a counter that faults; one that does not move; a wait no clock can time.
 */
KERNEL_TEST(a_counter_that_cannot_be_trusted_is_absent_by_its_reason)
{
    static KernelEnergyCounter_t found;
    EnergyCounterTestMachine_t machine = energy_counter_test_skylake();

    kernel_energy_counter_detect(KERNEL_ENERGY_COUNTER_BACKEND_NONE, energy_counter_test_read, energy_counter_test_wait,
                                 &machine, &found);
    kernel_test_check(test, found.state == KERNEL_ENERGY_COUNTER_NOT_LISTED && found.package_msr == 0u,
                      "no backend: not listed, and no register read");

    machine.msrs[0] = 0u;
    kernel_energy_counter_detect(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE, energy_counter_test_read,
                                 energy_counter_test_wait, &machine, &found);
    kernel_test_check(test, found.state == KERNEL_ENERGY_COUNTER_FAULTED, "a unit register that faults");

    machine = energy_counter_test_skylake();
    machine.values[0] = 0u;
    machine.package_step = 0u;
    kernel_energy_counter_detect(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE, energy_counter_test_read,
                                 energy_counter_test_wait, &machine, &found);
    kernel_test_check(test, found.state == KERNEL_ENERGY_COUNTER_NO_UNIT, "a unit register of 0, as KVM's: no unit");

    machine = energy_counter_test_skylake();
    machine.package_msr = 0u;
    kernel_energy_counter_detect(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE, energy_counter_test_read,
                                 energy_counter_test_wait, &machine, &found);
    kernel_test_check(test, found.state == KERNEL_ENERGY_COUNTER_FAULTED, "a counter that faults");

    machine = energy_counter_test_skylake();
    machine.package_step = 0u;
    kernel_energy_counter_detect(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE, energy_counter_test_read,
                                 energy_counter_test_wait, &machine, &found);
    kernel_test_check(test, found.state == KERNEL_ENERGY_COUNTER_FROZEN, "a counter that does not move: frozen");

    machine = energy_counter_test_skylake();
    machine.timed = false;
    kernel_energy_counter_detect(KERNEL_ENERGY_COUNTER_BACKEND_INTEL_CORE, energy_counter_test_read,
                                 energy_counter_test_wait, &machine, &found);
    kernel_test_check(test, found.state == KERNEL_ENERGY_COUNTER_UNTIMED, "a wait no clock times: untimed");
}

/**
 * @brief AMD's counter is read at AMD's registers, its unit at 0xC0010299, its package counter at 0xC001029B, and no
 *        Intel power field is read.
 */
KERNEL_TEST(amd_s_counter_is_read_at_amd_s_registers)
{
    static KernelEnergyCounter_t found;
    EnergyCounterTestMachine_t machine = energy_counter_test_skylake();

    machine.msrs[0] = KERNEL_ENERGY_COUNTER_AMD_UNIT_MSR;
    machine.values[0] = 0x000A1003u;
    machine.package_msr = KERNEL_ENERGY_COUNTER_AMD_PACKAGE_MSR;
    kernel_energy_counter_detect(KERNEL_ENERGY_COUNTER_BACKEND_AMD, energy_counter_test_read, energy_counter_test_wait,
                                 &machine, &found);
    kernel_test_check(test,
                      found.state == KERNEL_ENERGY_COUNTER_PRESENT &&
                          found.package_msr == KERNEL_ENERGY_COUNTER_AMD_PACKAGE_MSR &&
                          found.energy_unit_exponent == 16u,
                      "present at AMD's registers, ESU 16");
    kernel_test_check(test, !found.thermal_design_power_known && !found.power_limit_known,
                      "with no Intel power field read");
}

/**
 * @brief The boot detected this processor's counter, with the backend its identity calls for, read apart; a counter
 *        that is not present gives no sample. Under KVM, whose unit register reads 0, it has no unit; under TCG, whose
 *        AMD model sets no RAPL bit, it is not listed.
 */
KERNEL_TEST(the_boot_detected_this_processor_s_counter)
{
    CpuIdentity_t identity;
    uint32_t eax = 0u, ebx = 0u, ecx = 0u, edx = 0u;
    uint32_t counts = 0u;
    const KernelEnergyCounter_t *found = kernel_energy_counter();

    cpu_identity_read(&identity);
    asmutils_cpuid(0x80000000u, 0u, &eax, &ebx, &ecx, &edx);
    const uint32_t extended = eax;
    edx = 0u;
    if (extended >= 0x80000007u)
        asmutils_cpuid(0x80000007u, 0u, &eax, &ebx, &ecx, &edx);

    kernel_test_check(test, found->state != KERNEL_ENERGY_COUNTER_NOT_DETECTED, "the boot detected it");
    kernel_test_check(test, found->backend == kernel_energy_counter_backend_of(&identity, edx),
                      "with the backend its identity calls for");
    kernel_test_check(test, found->state == KERNEL_ENERGY_COUNTER_PRESENT || !kernel_energy_counter_sample(&counts),
                      "and no sample where it is not present");
    if (identity.hypervisor && memcmp(identity.hypervisor_signature, "KVMKVMKVM", sizeof("KVMKVMKVM")) == 0 &&
        found->backend != KERNEL_ENERGY_COUNTER_BACKEND_NONE)
        kernel_test_check(test, found->state == KERNEL_ENERGY_COUNTER_NO_UNIT, "KVM's unit register of 0: no unit");
    kernel_test_measure(test, "backend", found->backend);
    kernel_test_measure(test, "state", found->state);
}
