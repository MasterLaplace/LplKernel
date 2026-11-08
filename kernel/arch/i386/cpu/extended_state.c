#include <kernel/cpu/extended_state.h>
#include <kernel/lib/asmutils.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
#define EXTENDED_STATE_FCW_AFTER_FNINIT 0x037Fu     /**< The x87 control word FNINIT leaves. */
#define EXTENDED_STATE_CPUID_SAVES      1u /**< CPUID leaf 0xD's sub-leaf 1: XSAVEOPT, XSAVES, compacted size. */
#define EXTENDED_STATE_CPUID_XSAVEOPT   (1u << 0u)    /**< CPUID.(0xD,1):EAX.XSAVEOPT. */
#define EXTENDED_STATE_CPUID_XSAVES     (1u << 3u)    /**< CPUID.(0xD,1):EAX.XSAVES, with XRSTORS and IA32_XSS. */
#define EXTENDED_STATE_SUPERVISOR_MSR   0x0DA0u       /**< IA32_XSS: the supervisor components XSAVES saves too. */
#define EXTENDED_STATE_FXSAVE_BYTES     512u          /**< The FXSAVE image, which is XSAVE's legacy region too. */
#define EXTENDED_STATE_CONTROL_WORD     0u            /**< Where the legacy region holds the x87 control word. */
#define EXTENDED_STATE_SIMD_CONTROL     24u           /**< Where it holds the MXCSR. */
#define EXTENDED_STATE_COMPACTION       520u          /**< The header's XCOMP_BV, which XRSTORS reads. */
#define EXTENDED_STATE_COMPACTED        (1ull << 63u) /**< XCOMP_BV's bit 63: the area is in the compacted format. */

static uint32_t extended_state_offered_saves = 1u << EXTENDED_STATE_SAVE_FXSAVE; /**< One bit per save offered. */
static uint32_t extended_state_standard_bytes = EXTENDED_STATE_FXSAVE_BYTES;     /**< An XSAVE area's size. */
static uint32_t extended_state_compacted_bytes = EXTENDED_STATE_FXSAVE_BYTES;    /**< An XSAVES area's size. */
static uint32_t extended_state_enabled_components = 0u;                          /**< What XCR0 enables. */

/**
 * @brief Records which saves the processor offers and the sizes of their areas, once XCR0 enables @p components:
 *        CPUID leaf 0xD gives the sizes for the components enabled then. IA32_XSS is cleared first where XSAVES
 *        exists, so XSAVES saves no supervisor component.
 */
static void extended_state_record_saves(uint32_t components)
{
    uint32_t standard_bytes = 0u;
    uint32_t saves = 0u;
    uint32_t compacted_bytes = 0u;

    asmutils_cpuid(EXTENDED_STATE_CPUID_COMPONENTS, EXTENDED_STATE_CPUID_SAVES, &saves, NULL, NULL, NULL);
    if ((saves & EXTENDED_STATE_CPUID_XSAVES) != 0u)
        asmutils_write_model_specific_register(EXTENDED_STATE_SUPERVISOR_MSR, 0u);
    asmutils_cpuid(EXTENDED_STATE_CPUID_COMPONENTS, 0u, NULL, &standard_bytes, NULL, NULL);
    asmutils_cpuid(EXTENDED_STATE_CPUID_COMPONENTS, EXTENDED_STATE_CPUID_SAVES, NULL, &compacted_bytes, NULL, NULL);
    extended_state_offered_saves =
        ((components & ~(EXTENDED_STATE_X87 | EXTENDED_STATE_SSE)) == 0u ? 1u << EXTENDED_STATE_SAVE_FXSAVE : 0u) |
        (1u << EXTENDED_STATE_SAVE_XSAVE) |
        ((saves & EXTENDED_STATE_CPUID_XSAVEOPT) != 0u ? 1u << EXTENDED_STATE_SAVE_XSAVEOPT : 0u) |
        ((saves & EXTENDED_STATE_CPUID_XSAVES) != 0u ? 1u << EXTENDED_STATE_SAVE_XSAVES : 0u);
    extended_state_standard_bytes = standard_bytes;
    extended_state_compacted_bytes = compacted_bytes;
    extended_state_enabled_components = components;
}

/**
 * @brief Writes XCR0 with the components extended_state_components_to_enable keeps; CR4.OSXSAVE is set.
 */
__attribute__((target("xsave"))) static void extended_state_enable_components(void)
{
    uint32_t offered = 0u;

    asmutils_cpuid(EXTENDED_STATE_CPUID_COMPONENTS, 0u, &offered, NULL, NULL, NULL);

    const uint32_t components = extended_state_components_to_enable(offered);

    __builtin_ia32_xsetbv(0u, (unsigned long long) components);
    extended_state_record_saves(components);
}

/**
 * @brief Writes the @p bytes low bytes of @p value at @p at, little-endian as the processor reads them.
 */
static void extended_state_write_bytes(uint8_t *at, uint64_t value, uint32_t bytes)
{
    for (uint32_t byte = 0u; byte < bytes; ++byte)
        at[byte] = (uint8_t) (value >> (8u * byte));
}

/**
 * @brief Loads the MXCSR of a reset; CR4.OSFXSR is set.
 */
__attribute__((target("sse"))) static void extended_state_load_default_simd_control(void)
{
    __builtin_ia32_ldmxcsr(EXTENDED_STATE_MXCSR_DEFAULT);
}

uint32_t extended_state_components_to_enable(uint32_t offered)
{
    uint32_t components =
        offered & (EXTENDED_STATE_X87 | EXTENDED_STATE_SSE | EXTENDED_STATE_AVX | EXTENDED_STATE_AVX_512);

    if ((components & EXTENDED_STATE_SSE) == 0u)
        components &= ~(EXTENDED_STATE_AVX | EXTENDED_STATE_AVX_512);
    if ((components & EXTENDED_STATE_AVX) == 0u || (components & EXTENDED_STATE_AVX_512) != EXTENDED_STATE_AVX_512)
        components &= ~EXTENDED_STATE_AVX_512;
    return components | EXTENDED_STATE_X87;
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

ExtendedStateSave_t extended_state_preferred_save(void)
{
    if (extended_state_offers_save(EXTENDED_STATE_SAVE_XSAVES))
        return EXTENDED_STATE_SAVE_XSAVES;
    if (extended_state_offers_save(EXTENDED_STATE_SAVE_XSAVEOPT))
        return EXTENDED_STATE_SAVE_XSAVEOPT;
    if (extended_state_offers_save(EXTENDED_STATE_SAVE_XSAVE))
        return EXTENDED_STATE_SAVE_XSAVE;
    return EXTENDED_STATE_SAVE_FXSAVE;
}

bool extended_state_offers_save(ExtendedStateSave_t save)
{
    return save <= EXTENDED_STATE_SAVE_XSAVES && (extended_state_offered_saves & (1u << save)) != 0u;
}

uint32_t extended_state_area_size(ExtendedStateSave_t save)
{
    if (save == EXTENDED_STATE_SAVE_XSAVES)
        return extended_state_compacted_bytes;
    if (save == EXTENDED_STATE_SAVE_FXSAVE)
        return EXTENDED_STATE_FXSAVE_BYTES;
    return extended_state_standard_bytes;
}

void extended_state_prepare_area(void *area, ExtendedStateSave_t save)
{
    uint8_t *const bytes = (uint8_t *) area;

    memset(bytes, 0, extended_state_area_size(save));
    extended_state_write_bytes(bytes + EXTENDED_STATE_CONTROL_WORD, EXTENDED_STATE_FCW_AFTER_FNINIT, 2u);
    extended_state_write_bytes(bytes + EXTENDED_STATE_SIMD_CONTROL, EXTENDED_STATE_MXCSR_DEFAULT, 4u);
    if (save == EXTENDED_STATE_SAVE_XSAVES)
        extended_state_write_bytes(bytes + EXTENDED_STATE_COMPACTION,
                                   EXTENDED_STATE_COMPACTED | extended_state_enabled_components, 8u);
}
