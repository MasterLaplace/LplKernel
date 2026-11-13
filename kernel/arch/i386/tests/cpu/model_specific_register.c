#include <kernel/cpu/cpu_identity.h>
#include <kernel/cpu/exception.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(model_specific_register, KERNEL_TEST_STAGE_BOOTED);

/** IA32_TIME_STAMP_COUNTER, which every processor has. */
#define MODEL_SPECIFIC_REGISTER_TEST_TIME_STAMP_COUNTER 0x10u
/** An index no processor implements. */
#define MODEL_SPECIFIC_REGISTER_TEST_NONE 0x12345678u
/** What a refused read leaves in its value. */
#define MODEL_SPECIFIC_REGISTER_TEST_UNTOUCHED 0xDEADBEEFDEADBEEFull

/**
 * @brief A register the processor has is given by the fault-tolerant read, as the plain read gives it.
 */
KERNEL_TEST(a_register_the_processor_has_is_given)
{
    uint64_t value = MODEL_SPECIFIC_REGISTER_TEST_UNTOUCHED;
    const uint32_t refused_before = interrupt_exception_refused_model_specific_register_reads();

    kernel_test_check(
        test, asmutils_read_model_specific_register_or_refuse(MODEL_SPECIFIC_REGISTER_TEST_TIME_STAMP_COUNTER, &value),
        "the time-stamp counter is given");
    kernel_test_check(test, value != MODEL_SPECIFIC_REGISTER_TEST_UNTOUCHED, "and written out");
    kernel_test_check(test, interrupt_exception_refused_model_specific_register_reads() == refused_before,
                      "with no refusal counted");
}

/**
 * @brief Under KVM, a read of a register the processor lacks raises #GP, which the handler turns into a refusal: the
 *        call returns false, leaves the value untouched, counts it, and the boot goes on. TCG gives every MSR as 0
 *        without a fault, so the refusal is not seen there.
 */
KERNEL_TEST(a_register_the_processor_lacks_is_refused_rather_than_fatal)
{
    CpuIdentity_t identity;
    uint64_t value = MODEL_SPECIFIC_REGISTER_TEST_UNTOUCHED;

    cpu_identity_read(&identity);
    if (!identity.hypervisor || memcmp(identity.hypervisor_signature, "KVMKVMKVM", sizeof("KVMKVMKVM")) != 0)
    {
        kernel_test_skip(test, "not KVM: TCG gives every MSR as 0, and a real processor's set is its own");
        return;
    }
    const uint32_t refused_before = interrupt_exception_refused_model_specific_register_reads();
    const bool given = asmutils_read_model_specific_register_or_refuse(MODEL_SPECIFIC_REGISTER_TEST_NONE, &value);

    kernel_test_check(test, !given, "an MSR no processor has is refused");
    kernel_test_check(test, value == MODEL_SPECIFIC_REGISTER_TEST_UNTOUCHED, "its value left untouched");
    kernel_test_check(test, interrupt_exception_refused_model_specific_register_reads() == refused_before + 1u,
                      "and the refusal counted");
}
