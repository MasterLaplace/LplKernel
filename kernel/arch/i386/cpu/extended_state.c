#include <kernel/cpu/extended_state.h>
#include <kernel/lib/asmutils.h>

#include <stdbool.h>
#include <stdint.h>

#define EXTENDED_STATE_CR0_MP           (1u << 1u)  /**< CR0.MP: WAIT and FWAIT honour TS. */
#define EXTENDED_STATE_CR0_EM           (1u << 2u)  /**< CR0.EM: x87 instructions fault, SSE ones raise #UD. */
#define EXTENDED_STATE_CR0_TS           (1u << 3u)  /**< CR0.TS: the next x87 or SSE instruction faults. */
#define EXTENDED_STATE_CR0_NE           (1u << 5u)  /**< CR0.NE: x87 errors raise #MF rather than FERR#. */
#define EXTENDED_STATE_CR4_OSFXSR       (1u << 9u)  /**< CR4.OSFXSR: FXSAVE and FXRSTOR cover SSE, SSE runs. */
#define EXTENDED_STATE_CR4_OSXMMEXCPT   (1u << 10u) /**< CR4.OSXMMEXCPT: an unmasked SIMD error raises #XM. */
#define EXTENDED_STATE_CR4_OSXSAVE      (1u << 18u) /**< CR4.OSXSAVE: XSAVE, XGETBV and XSETBV are enabled. */
#define EXTENDED_STATE_CPUID_FEATURES   0x1u        /**< CPUID leaf 1. */
#define EXTENDED_STATE_CPUID_XSAVE      (1u << 26u) /**< CPUID.1:ECX.XSAVE. */
#define EXTENDED_STATE_CPUID_COMPONENTS 0xDu        /**< CPUID leaf 0xD, sub-leaf 0: the XCR0 components offered. */
#define EXTENDED_STATE_X87              0x01u       /**< XCR0 bit 0, which XSETBV requires. */
#define EXTENDED_STATE_SSE              0x02u       /**< XCR0 bit 1. */
#define EXTENDED_STATE_AVX              0x04u       /**< XCR0 bit 2, which requires bit 1. */
#define EXTENDED_STATE_AVX_512          0xE0u       /**< XCR0 bits 5 to 7, set together, which require bits 1 and 2. */
#define EXTENDED_STATE_MXCSR_DEFAULT    0x1F80u     /**< Every SIMD exception masked, round to nearest. */

/**
 * @brief The components XCR0 enables among the @p offered ones: x87, SSE, AVX and the three AVX-512 ones, each only
 *        with those XSETBV requires it with.
 */
static uint32_t extended_state_components_to_enable(uint32_t offered)
{
    uint32_t components =
        offered & (EXTENDED_STATE_X87 | EXTENDED_STATE_SSE | EXTENDED_STATE_AVX | EXTENDED_STATE_AVX_512);

    if ((components & EXTENDED_STATE_SSE) == 0u)
        components &= ~(EXTENDED_STATE_AVX | EXTENDED_STATE_AVX_512);
    if ((components & EXTENDED_STATE_AVX) == 0u || (components & EXTENDED_STATE_AVX_512) != EXTENDED_STATE_AVX_512)
        components &= ~EXTENDED_STATE_AVX_512;
    return components | EXTENDED_STATE_X87;
}

/**
 * @brief Writes XCR0 with the components extended_state_components_to_enable keeps; CR4.OSXSAVE is set.
 */
__attribute__((target("xsave"))) static void extended_state_enable_components(void)
{
    uint32_t offered = 0u;

    asmutils_cpuid(EXTENDED_STATE_CPUID_COMPONENTS, 0u, &offered, NULL, NULL, NULL);
    __builtin_ia32_xsetbv(0u, (unsigned long long) extended_state_components_to_enable(offered));
}

/**
 * @brief Loads the MXCSR of a reset; CR4.OSFXSR is set.
 */
__attribute__((target("sse"))) static void extended_state_load_default_simd_control(void)
{
    __builtin_ia32_ldmxcsr(EXTENDED_STATE_MXCSR_DEFAULT);
}

void extended_state_initialize_on_this_processor(void)
{
    uint32_t features_ecx = 0u;

    asmutils_cpuid(EXTENDED_STATE_CPUID_FEATURES, 0u, NULL, NULL, &features_ecx, NULL);

    const bool xsave = (features_ecx & EXTENDED_STATE_CPUID_XSAVE) != 0u;
    const uint32_t control_register_0 =
        (asmutils_read_control_register_0() & ~(EXTENDED_STATE_CR0_EM | EXTENDED_STATE_CR0_TS)) |
        EXTENDED_STATE_CR0_MP | EXTENDED_STATE_CR0_NE;
    const uint32_t control_register_4 = asmutils_read_control_register_4() | EXTENDED_STATE_CR4_OSFXSR |
                                        EXTENDED_STATE_CR4_OSXMMEXCPT | (xsave ? EXTENDED_STATE_CR4_OSXSAVE : 0u);

    asmutils_write_control_register_0(control_register_0);
    asmutils_write_control_register_4(control_register_4);
    if (xsave)
        extended_state_enable_components();
    asmutils_initialize_floating_point_unit();
    extended_state_load_default_simd_control();
}
