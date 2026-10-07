#include "libengine/libengine.h"

#include <lpl/testing/Runner.hpp>

namespace {

/**
 * @brief The kernel's port, as the runner of lpl::testing writes to it.
 */
class KernelSink final : public lpl::testing::Sink {
public:
    KernelSink(libengine_test_write_t write, void *context) noexcept : _write(write), _context(context) {}

    void write(std::string_view text) noexcept override { _write(_context, text.data(), text.size()); }

private:
    libengine_test_write_t _write;
    void *_context;
};

} // namespace

extern "C" uint32_t libengine_test_suite_count(void) { return lpl::testing::suiteCount(); }

extern "C" libengine_test_totals_t libengine_test_run(libengine_test_write_t write, void *context,
                                                      const char *selection, uint32_t first_suite_number)
{
    KernelSink sink(write, context);
    const lpl::testing::Totals totals = lpl::testing::run(sink, selection, first_suite_number);

    return libengine_test_totals_t{totals.passed, totals.failed, totals.skipped, totals.checks, totals.selected};
}
