#include <kernel/cpu/cpu_identity.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(cpu_identity, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief Whether every character of @p text is printable ASCII, and there are @p length of them.
 */
static bool cpu_identity_test_is_printable(const char *text, uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index)
    {
        if (text[index] < ' ' || text[index] > '~')
            return false;
    }
    return text[length] == '\0';
}

/**
 * @brief The processor names its vendor in twelve printable characters and a family, and a
 *        hypervisor that sets its bit is measured with its signature.
 *
 * @details The signature is measured, not checked: a hypervisor may set the bit and leave leaf
 *          0x40000000 empty, and the machine record then says so.
 */
KERNEL_TEST(the_processor_names_itself)
{
    CpuIdentity_t identity;

    cpu_identity_read(&identity);
    kernel_test_check(test, cpu_identity_test_is_printable(identity.vendor, 12u),
                      "the vendor is twelve printable characters");
    kernel_test_check(test, identity.family != 0u, "and the processor has a family");

    kernel_test_measure(test, "family", identity.family);
    kernel_test_measure(test, "model", identity.model);
    kernel_test_measure(test, "stepping", identity.stepping);
    kernel_test_measure(test, "hypervisor", identity.hypervisor ? 1u : 0u);
    kernel_test_note(test, identity.vendor);
    if (identity.hypervisor_signature[0] != '\0')
        kernel_test_note(test, identity.hypervisor_signature);
}
