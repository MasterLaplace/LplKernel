#include "predecoding_runs.h"

#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <stddef.h>

#define PREDECODING_FOLD_OFFSET 0x811C9DC5u /**< FNV-1a's offset basis. */
#define PREDECODING_FOLD_PRIME  0x01000193u /**< FNV-1a's prime. */

static uint8_t predecoding_runs_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY + 256u];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t predecoding_runs_host;

AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t predecoding_load;

/**
 * @brief Folds 8 little-endian bytes into an FNV-1a fold.
 */
static uint32_t predecoding_fold_word(uint32_t fold, uint64_t word)
{
    for (uint32_t byte = 0u; byte < 8u; ++byte)
        fold = (fold ^ (uint32_t) ((word >> (8u * byte)) & 0xFFu)) * PREDECODING_FOLD_PRIME;
    return fold;
}

/**
 * @brief Folds @p count counters into an FNV-1a fold, each as 8 little-endian bytes.
 */
static uint32_t predecoding_fold_counts(uint32_t fold, const uint32_t *counts, uint32_t count)
{
    for (uint32_t index = 0u; index < count; ++index)
        fold = predecoding_fold_word(fold, counts[index]);
    return fold;
}

/**
 * @brief Folds every access the simulated host logged: its kind, width, port or offset, value and address.
 */
static uint32_t predecoding_fold_accesses(uint32_t fold)
{
    const uint32_t logged =
        (predecoding_runs_host.access_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES) ?
            predecoding_runs_host.access_count :
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES;

    fold = predecoding_fold_word(fold, predecoding_runs_host.access_count);
    for (uint32_t index = 0u; index < logged; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t *access = &predecoding_runs_host.accesses[index];

        fold = predecoding_fold_word(fold, ((uint64_t) access->kind << 48u) | ((uint64_t) access->width << 40u) |
                                               ((uint64_t) access->port << 32u) | access->value);
        fold = predecoding_fold_word(fold, access->address);
    }
    return fold;
}

/**
 * @brief Folds what an evaluation left: its status and result, everything the evaluations of the load
 *        counted, the opcodes they executed and those that failed as unimplemented included, the namespace's
 *        lookups, probes and nodes, the heap's objects, the field accesses, per space and AccessType too,
 *        every access the host saw, and the namespace itself.
 */
static uint32_t predecoding_fold(AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status,
                                 const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *result)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &predecoding_load.evaluation;
    const uint64_t words[] = {
        status,
        result->kind,
        result->payload,
        evaluation->iterations,
        evaluation->back_edges,
        evaluation->runs,
        evaluation->failures,
        evaluation->first_failure,
        evaluation->implicit_returns,
        evaluation->uninitialized_operands,
        evaluation->debug_stores,
        evaluation->mixed_type_comparisons,
        evaluation->forbidden_conversions,
        evaluation->references_built,
        evaluation->frames_peak,
        evaluation->values_peak,
        evaluation->operators_peak,
        evaluation->blocks_peak,
        evaluation->iterations_peak,
        evaluation->name_indirections_peak,
        predecoding_load.temporary_nodes_peak,
        predecoding_load.space.lookups,
        predecoding_load.space.probes,
        predecoding_load.space.live_nodes,
        predecoding_load.heap.objects,
        predecoding_load.heap.objects_freed,
        predecoding_load.regions.field_reads,
        predecoding_load.regions.field_fast_path,
        predecoding_load.regions.field_writes,
        advanced_configuration_and_power_interface_machine_language_namespace_fold(&predecoding_load),
    };
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t *regions = &predecoding_load.regions;
    uint32_t fold = PREDECODING_FOLD_OFFSET;

    for (uint32_t index = 0u; index < sizeof(words) / sizeof(words[0]); ++index)
        fold = predecoding_fold_word(fold, words[index]);
    fold = predecoding_fold_counts(fold, evaluation->statuses,
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT);
    fold = predecoding_fold_counts(fold, evaluation->unimplemented,
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT);
    fold = predecoding_fold_counts(fold, evaluation->executed,
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT);
    fold = predecoding_fold_counts(fold, regions->space_accesses,
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SPACES);
    fold = predecoding_fold_counts(fold, regions->type_accesses,
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ACCESS_TYPES);
    return predecoding_fold_accesses(fold);
}

/**
 * @brief What the load counted of its records into @p run.
 */
static void predecoding_counts(PredecodingRun_t *run)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t *cache = &predecoding_load.predecoding;

    run->records_run = cache->records_run;
    run->declined = 0u;
    for (uint32_t reason = 0u; reason < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINES; ++reason)
        run->declined += cache->declined[reason];
    run->declined_names = cache->declined[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_NAME];
    run->stale_names = cache->stale_names;
    run->flushes = cache->flushes;
    run->records_peak = cache->records_peak;
    run->nodes_peak = cache->nodes_peak;
    run->constants_peak = cache->constants_peak;
    run->names_peak = cache->names_peak;
    run->built = cache->built;
    run->tight_turns = cache->tight_turns;
    run->searches = cache->searches;
    run->loops = cache->handed_back_loops;
    run->field_reads = predecoding_load.regions.field_reads;
    run->field_fast_path = predecoding_load.regions.field_fast_path;
}

bool predecoding_open_empty(KernelTest_t *test, bool predecodes)
{
    advanced_configuration_and_power_interface_simulated_host_open(&predecoding_runs_host);
    if (!kernel_test_check(
            test,
            advanced_configuration_and_power_interface_machine_language_open(
                &predecoding_load, &predecoding_runs_host.host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
            "the namespace opens"))
        return false;
    predecoding_load.predecodes_loops = predecodes;
    return true;
}

void predecoding_declare(uint8_t *table, const char *signature, uint8_t revision, const uint8_t *aml, uint32_t length)
{
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &predecoding_load, made_up_definition_block(table, signature, revision, aml, length));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&predecoding_load);
}

bool predecoding_open_booted(KernelTest_t *test, const uint8_t *aml, uint32_t length)
{
    advanced_configuration_and_power_interface_simulated_host_open(&predecoding_runs_host);
    if (!kernel_test_check(
            test,
            advanced_configuration_and_power_interface_machine_language_open(
                &predecoding_load, &predecoding_runs_host.host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
            "the namespace opens"))
        return false;
    predecoding_declare(predecoding_runs_table, "DSDT", 2u, aml, length);
    return true;
}

bool predecoding_open(KernelTest_t *test, uint8_t revision, const uint8_t *aml, uint32_t length, bool predecodes)
{
    if (!predecoding_open_empty(test, predecodes))
        return false;
    predecoding_declare(predecoding_runs_table, "DSDT", revision, aml, length);
    return true;
}

void predecoding_poke(uint64_t address, const void *bytes, uint32_t length)
{
    (void) advanced_configuration_and_power_interface_simulated_host_poke(&predecoding_runs_host, address, bytes,
                                                                          length);
}

void predecoding_close(void)
{
    advanced_configuration_and_power_interface_machine_language_close(&predecoding_load);
    advanced_configuration_and_power_interface_simulated_host_close(&predecoding_runs_host);
}

void predecoding_evaluate(const char *path, uint64_t argument, PredecodingRun_t *run)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result = {0};
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t given = {
        .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
        .payload = argument,
    };
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate(
            &predecoding_load, path, (argument == PREDECODING_NO_ARGUMENT) ? NULL : &given,
            (argument == PREDECODING_NO_ARGUMENT) ? 0u : 1u, &result);

    run->fold = predecoding_fold(status, &result);
    run->status = status;
    run->result = result.payload;
    predecoding_counts(run);
    advanced_configuration_and_power_interface_machine_language_value_release(&predecoding_load.heap,
                                                                              &predecoding_load.space, &result);
}

bool predecoding_run(KernelTest_t *test, const uint8_t *aml, uint32_t length, uint8_t revision, const char *path,
                     uint64_t argument, bool predecodes, PredecodingRun_t *run)
{
    if (!predecoding_open(test, revision, aml, length, predecodes))
        return false;
    predecoding_evaluate(path, argument, run);
    predecoding_close();
    return true;
}
