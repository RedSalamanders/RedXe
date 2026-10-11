#include "Settings.Tests.JsonStorage.h"
#include "../../RedXe/Settings.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

struct JsonObjectSettingsTestAccess final
{
    static std::weak_ptr<const char[]> Owner(const JsonObjectSettings& value) noexcept
    {
        return value._utf8;
    }
};
static_assert(std::is_nothrow_copy_constructible_v<JsonObjectSettings> &&
              std::is_nothrow_copy_assignable_v<JsonObjectSettings> &&
              std::is_nothrow_move_constructible_v<JsonObjectSettings> &&
              std::is_nothrow_move_assignable_v<JsonObjectSettings>);
namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void TestImmutableOwnership()
{
    JsonObjectSettings value;
    Require(value.View() == "{}" && value.Bytes() == 2 && value.Data()[2] == '\0' && value.OwnedPayloadBytes() == 0,
            "default JSON owns no buffer");
    std::string authored = R"({"seed":7})";
    Require(value.Assign(authored) == S_OK && value.OwnedPayloadBytes() == authored.size() + 1 &&
                value.Data()[value.Bytes()] == '\0',
            "exact payload and terminal byte");
    authored[8] = '9';
    Require(value.View() == R"({"seed":7})", "borrowed source was copied");
    JsonObjectSettings saved = value;
    const auto oldOwner = JsonObjectSettingsTestAccess::Owner(value);
    Require(saved.Data() == value.Data(), "settings copy shares immutable bytes");
    Require(value.Assign(R"({"seed":11})") == S_OK && value.Data() != saved.Data() && saved.View() == R"({"seed":7})" &&
                !oldOwner.expired(),
            "copy survives replacement without mutation");
    JsonObjectSettings distinct;
    Require(distinct.Assign(saved.View()) == S_OK && distinct == saved && distinct.Data() != saved.Data(),
            "distinct owners compare content");
    Require(value.Assign("{}") == S_OK && value.OwnedPayloadBytes() == 0 && !oldOwner.expired(),
            "reset only releases the replaced owner's lease");
    Require(saved.Assign("{}") == S_OK && oldOwner.expired(), "last copy releases its payload");
    const auto movedOwner = JsonObjectSettingsTestAccess::Owner(distinct);
    JsonObjectSettings moved = std::move(distinct);
    Require(distinct.View() == "{}" && distinct.Bytes() == 2 && distinct.OwnedPayloadBytes() == 0 &&
                moved.View() == R"({"seed":7})" && !movedOwner.expired(),
            "moved-from view is safe and destination owns the bytes");
    moved = {};
    Require(movedOwner.expired(), "last moved owner releases its payload");
}
void TestBoundsAndFailure()
{
    JsonObjectSettings value;
    Require(SetJsonObjectSettings(R"({"seed":7})", value) == S_OK, "initial validated JSON");
    const JsonObjectSettings saved = value;
    const char* const identity = value.Data();
    const std::string maximum = R"({"k":")" + std::string(kPrivateConfigurationCapacity - 8, 'a') + R"("})";
    Require(maximum.size() == kPrivateConfigurationCapacity && SetJsonObjectSettings(maximum, value) == S_OK &&
                value.Bytes() == kPrivateConfigurationCapacity && value.Data()[value.Bytes()] == '\0',
            "maximum valid JSON extent remains accepted");
    const JsonObjectSettings maximumSaved = value;
    const std::string oversized = maximum + " ";
    const std::string embeddedNul{"{\0}", 3};
    for (const std::string_view invalid : std::array<std::string_view, 4>{"", "{", oversized, embeddedNul})
        Require(value.Assign(invalid) == E_INVALIDARG && value == maximumSaved,
                "invalid raw extent preserves the previous owner");
    Require(FAILED(SetJsonObjectSettings(oversized, value)) && FAILED(SetJsonObjectSettings("[]", value)) &&
                FAILED(SetJsonObjectSettings("{bad}", value)) && value == maximumSaved,
            "strict object parsing remains transactional");
    for (uint32_t phase : {1U, 2U})
    {
        value = saved;
        FailJsonObjectAllocationForTesting(phase);
        const JsonObjectSettings copy = value;
        Require(value.Assign(value.View()) == S_OK && copy.Data() == identity && value.Data() == identity,
                "copy and unchanged assignment allocate nothing");
        Require(value.Assign(R"({"seed":11})") == E_OUTOFMEMORY && value.Data() == identity && value == saved &&
                    copy == saved && value.Assign(R"({"seed":11})") == S_OK && copy == saved,
                "payload and shared-owner faults preserve old copies and allow retry");
        JsonObjectSettings cold;
        FailJsonObjectAllocationForTesting(phase);
        Require(cold.Assign(R"({"seed":11})") == E_OUTOFMEMORY && cold.OwnedPayloadBytes() == 0 &&
                    cold.View() == "{}" && cold.Assign(R"({"seed":11})") == S_OK,
                "failed first assignment publishes no owner and retries");
    }
}
void TestSettingsTransactions()
{
    constexpr std::string_view document =
        R"({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.matrix-rain","seed":7}]}]})";
    AppSettings original;
    Require(ParseAppSettingsJson(document, original) == S_OK, "transaction fixture parses");
    const AppSettings saved = original;
    const std::string previousPrivate{saved.dashboard.pages[0].widgets[0].privateConfiguration.View()};
    const auto id = original.dashboard.pages[0].widgets[0].id.View();
    for (uint32_t phase : {1U, 2U})
    {
        AppSettings candidate = saved;
        FailJsonObjectAllocationForTesting(phase);
        Require(PatchWidgetInstanceSettings(candidate, id, R"({"seed":7})") == S_FALSE && candidate == saved,
                "unchanged merge reuses its owner and authored source");
        Require(PatchWidgetInstanceSettings(candidate, id, R"({"seed":9})") == E_OUTOFMEMORY && candidate == saved,
                "failed merged payload restores the whole authored and typed state");
        Require(PatchWidgetInstanceSettings(candidate, id, R"({"seed":9})") == S_OK, "merge retry succeeds");
        Require(candidate != saved &&
                    saved.dashboard.pages[0].widgets[0].privateConfiguration.View() == previousPrivate,
                "merge retry replaces only the candidate copy and preserves resolved defaults");
        candidate = saved;
        FailJsonObjectAllocationForTesting(phase);
        Require(FAILED(ParseAppSettingsJson(document, candidate)) && candidate == saved,
                "parser allocation failure never publishes partial typed state");
        Require(ParseAppSettingsJson(document, candidate) == S_OK && candidate == saved,
                "parser allocation retry preserves authored semantics");
    }
}
void TestConcurrentCopies()
{
    JsonObjectSettings value;
    Require(value.Assign(R"({"seed":7})") == S_OK, "concurrent-copy fixture");
    std::atomic<bool> valid{true};
    std::array<std::jthread, 4> readers;
    for (auto& reader : readers)
        reader = std::jthread(
            [&]() noexcept
            {
                for (uint32_t index = 0; index < 2048; ++index)
                {
                    JsonObjectSettings copied = value;
                    JsonObjectSettings moved = std::move(copied);
                    if (moved.Data() != value.Data() || moved.View() != R"({"seed":7})" || copied.View() != "{}")
                        valid.store(false, std::memory_order_relaxed);
                }
            });
    for (auto& reader : readers)
        reader.join();
    Require(valid.load(), "immutable shared payload survives concurrent copies");
}
} // namespace

HRESULT TestJsonObjectSettingsStorage() noexcept
{
    try
    {
        TestImmutableOwnership();
        TestBoundsAndFailure();
        TestSettingsTransactions();
        TestConcurrentCopies();
        std::wprintf(L"Immutable JSON ownership, bounds and allocation-failure transactions passed.\n");
        return S_OK;
    }
    catch (const std::exception& error)
    {
        std::printf("JSON storage: %s\n", error.what());
        return E_FAIL;
    }
    catch (...)
    {
        return E_FAIL;
    }
}
