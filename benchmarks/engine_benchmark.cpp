#include <windows_pointer/engine.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

constexpr std::array TRACE{
    windows_pointer::Motion{1, 0},
    windows_pointer::Motion{4, 1},
    windows_pointer::Motion{12, 5},
    windows_pointer::Motion{30, 12},
    windows_pointer::Motion{-7, 3},
    windows_pointer::Motion{-24, -9},
};
constexpr std::uint64_t REPORTS = 10'000'000;

template <typename DpiForReport>
void run(std::string_view name, DpiForReport dpiForReport) {
    windows_pointer::Engine engine;
    std::int64_t            checksum = 0;
    const auto              start    = std::chrono::steady_clock::now();

    for (std::uint64_t report = 0; report < REPORTS; ++report) {
        const auto output = engine.apply(TRACE[report % TRACE.size()], dpiForReport(report));
        checksum += output.x + output.y;
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto nanos   = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    std::cout << name << ": " << REPORTS << " reports in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms, "
              << static_cast<double>(nanos) / REPORTS << " ns/report"
              << " (ignore me: " << checksum << ")\n";
}

} // namespace

int main() {
    run("same display", [](std::uint64_t) { return 96; });
    run("dpi boundary every report", [](std::uint64_t report) { return report % 2 == 0 ? 96 : 144; });
}
