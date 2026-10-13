#include "../../Plugins/SystemData/NativeProcessQuery.h"
#include "../../Plugins/SystemData/NativeQuery.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
struct QueryFixture final
{
    void* expectedBuffer = nullptr;
    ULONG requiredBytes = 1536U * 1024U;
    ULONG reportedBytes = 1536U * 1024U;
    NTSTATUS status = 0;
    ULONG calls = 0;
    ULONG firstCapacity = 0;
    bool argumentsValid = true;
};
QueryFixture g_fixture;

NTSTATUS NTAPI QueryProcessFixture(SYSTEM_INFORMATION_CLASS infoClass, PVOID buffer, ULONG capacity,
                                   PULONG returned) noexcept
{
    ++g_fixture.calls;
    if (g_fixture.calls == 1)
        g_fixture.firstCapacity = capacity;
    g_fixture.argumentsValid &= infoClass == SystemProcessInformation && buffer == g_fixture.expectedBuffer && returned;
    if (!returned)
        return static_cast<NTSTATUS>(0xC000000DL);
    *returned = g_fixture.reportedBytes;
    return capacity < g_fixture.requiredBytes ? static_cast<NTSTATUS>(0xC0000004L) : g_fixture.status;
}

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

void TestNativeProcessQuery()
{
    std::vector<std::byte> storage(kRedXeNtProcessBufferBytes);
    g_fixture = {};
    g_fixture.expectedBuffer = storage.data();
    ULONG readable = 17;
    Require(RedXeNativeProcessQuery::Collect(QueryProcessFixture, storage, readable) &&
                readable == g_fixture.reportedBytes,
            "a process snapshot larger than one MiB must fit the existing buffer");
    Require(g_fixture.argumentsValid && g_fixture.calls == 1 && g_fixture.firstCapacity == storage.size(),
            "process collection must query its full existing capacity once, without an undersized kernel walk");

    const auto failsOnce = [&](ULONG required, ULONG reported, NTSTATUS status)
    {
        g_fixture = {};
        g_fixture.expectedBuffer = storage.data();
        g_fixture.requiredBytes = required;
        g_fixture.reportedBytes = reported;
        g_fixture.status = status;
        readable = 17;
        Require(!RedXeNativeProcessQuery::Collect(QueryProcessFixture, storage, readable) && readable == 0 &&
                    g_fixture.argumentsValid && g_fixture.calls == 1 && g_fixture.firstCapacity == storage.size(),
                "failed or invalid process queries must expose no bytes and perform no retry or buffer growth");
    };
    const ULONG capacity = static_cast<ULONG>(storage.size());
    failsOnce(capacity + 1, capacity + 1, 0); // The system outgrew the bounded storage: use the caller's fallback.
    failsOnce(1, 1, static_cast<NTSTATUS>(0xC0000022L)); // Access/policy failure is not a capacity retry.
    failsOnce(1, 0, 0);                                  // A success without ReturnLength cannot be parsed.
    failsOnce(1, capacity + 1, 0);                       // Even a success cannot authorize reading past the buffer.

    g_fixture = {};
    readable = 17;
    Require(!RedXeNativeProcessQuery::Collect(nullptr, storage, readable) && readable == 0 && g_fixture.calls == 0,
            "an unavailable native API must not publish stale readable bytes");
    readable = 17;
    Require(!RedXeNativeProcessQuery::Collect(QueryProcessFixture, {}, readable) && readable == 0 &&
                g_fixture.calls == 0,
            "an empty process buffer must not call the kernel");
    std::cout << "Native process query capacity and failure tests passed.\n";
}
