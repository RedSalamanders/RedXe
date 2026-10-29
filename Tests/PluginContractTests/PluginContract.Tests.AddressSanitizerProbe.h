#pragma once

#include <memory>

// Isolated expected-failure fixture, entered only with --asan-probe before ordinary tests.
// The volatile access deliberately retains the selected ARM64 model in this detection probe.
// Owner: RedXe test maintainers; review: 2027-01-31.
#pragma warning(push)
#if defined(_M_ARM64)
#pragma warning(disable : 4746)
#endif
__declspec(noinline) inline int RunAddressSanitizerProbe()
{
#if defined(__SANITIZE_ADDRESS__)
    auto allocation = std::make_unique<char[]>(8);
    allocation[0] = 1;
    volatile char* borrowed = allocation.get();
    allocation.reset();
    return *borrowed;
#else
    return 2;
#endif
}
#pragma warning(pop)
