#include "AVControlProtocolValidation.h"
#include "InventorySelection.h"
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
uint32_t checks = 0;
void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
AVControl::DeviceId Id(uint32_t value)
{
    AVControl::DeviceId id;
    Check(id.Assign("device-" + std::to_string(value)), "inventory fixture ID");
    return id;
}
template <typename Rows> bool Contains(const Rows& rows, uint32_t count, const AVControl::DeviceId& id)
{
    return std::any_of(rows.begin(), rows.begin() + count, [&](const auto& row) { return row.id == id; });
}
} // namespace

uint32_t RunInventorySelectionTests()
{
    using namespace AVControl;
    checks = 0;
    auto preferences = std::make_unique<InventoryPreferences>();
    Check(ValidInventoryPreferences(*preferences), "empty preference union is valid");
    const std::array defaults{Id(95), Id(94), Id(93)};
    for (uint32_t i = 80; i < 88; ++i)
        Check(preferences->outputs.Add(Id(i)), "late profile device admitted as a preference");
    auto inventory = std::make_unique<Inventory>();
    RankedInventory selection(inventory->outputs, inventory->outputCount);
    for (uint32_t i = 0; i < 100; ++i)
    {
        Endpoint row;
        row.id = Id(i);
        selection.Offer(row, InventoryRank(row.id, defaults, preferences->outputs));
    }
    Check(inventory->outputCount == MaximumOutputs && selection.Truncated(),
          "oversized enumeration stays bounded and reports overflow");
    for (const auto& id : defaults)
        Check(Contains(inventory->outputs, inventory->outputCount, id),
              "all three late audio defaults survive admission");
    for (uint32_t i = 80; i < 88; ++i)
        Check(Contains(inventory->outputs, inventory->outputCount, Id(i)),
              "late saved bindings displace ordinary endpoints");
    for (uint32_t i = 0; i < 21; ++i)
        Check(Contains(inventory->outputs, inventory->outputCount, Id(i)),
              "ordinary tie retains earliest enumeration order");
    Endpoint duplicate;
    duplicate.id = defaults[0];
    selection.Offer(duplicate, 0);
    Check(inventory->outputCount == MaximumOutputs, "repeated endpoint identity never consumes another slot");

    RankedInventory replacement(inventory->outputs, inventory->outputCount);
    const std::array changedDefaults{Id(99), Id(98), Id(97)};
    for (uint32_t i = 0; i < 100; ++i)
    {
        Endpoint row;
        row.id = Id(i);
        replacement.Offer(row, InventoryRank(row.id, changedDefaults, preferences->outputs));
    }
    for (const auto& id : changedDefaults)
        Check(Contains(inventory->outputs, inventory->outputCount, id),
              "new default membership replaces still-present former defaults");
    for (const auto& id : defaults)
        Check(!Contains(inventory->outputs, inventory->outputCount, id),
              "evicted active endpoints can release watch slots before replacement");

    // More references than the final list can hold still preserve all defaults and the earliest references.
    for (uint32_t i = 0; i < 100; ++i)
        (void)preferences->inputs.Add(Id(i));
    RankedInventory crowded(inventory->inputs, inventory->inputCount);
    for (uint32_t i = 0; i < 100; ++i)
    {
        Endpoint row;
        row.id = Id(i);
        crowded.Offer(row, InventoryRank(row.id, defaults, preferences->inputs));
    }
    for (const auto& id : defaults)
        Check(Contains(inventory->inputs, inventory->inputCount, id),
              "defaults outrank a full set of profile references");
    Check(Contains(inventory->inputs, inventory->inputCount, Id(28)) &&
              !Contains(inventory->inputs, inventory->inputCount, Id(29)),
          "reference overflow has deterministic priority instead of identity substitution");

    for (uint32_t i = 20; i < 24; ++i)
        (void)preferences->cameras.Add(Id(i));
    RankedInventory cameras(inventory->cameras, inventory->cameraCount);
    const std::array selected{Id(29)};
    for (uint32_t i = 0; i < 30; ++i)
    {
        CameraDescriptor row;
        row.id = Id(i);
        cameras.Offer(row, InventoryRank(row.id, selected, preferences->cameras));
    }
    Check(inventory->cameraCount == MaximumCameras && cameras.Truncated(),
          "camera admission has its independent smaller bound");
    Check(Contains(inventory->cameras, inventory->cameraCount, selected[0]),
          "selected late camera cannot be evicted by another reference");
    for (uint32_t i = 20; i < 24; ++i)
        Check(Contains(inventory->cameras, inventory->cameraCount, Id(i)), "late camera profile references survive");

    preferences->outputs.count = MaximumOutputs + 1;
    Check(!ValidInventoryPreferences(*preferences), "oversized incoming preference count rejected before indexing");
    preferences->outputs.count = 2;
    preferences->outputs.ids[1] = preferences->outputs.ids[0];
    Check(!ValidInventoryPreferences(*preferences), "duplicate incoming preferences rejected");
    preferences->outputs.count = 1;
    preferences->outputs.ids[0].length = MaximumDeviceIdBytes + 1;
    Check(!ValidInventoryPreferences(*preferences), "oversized preference identity rejected without truncation");
    *preferences = {};
    for (uint32_t i = 0; i < 40; ++i)
    {
        Profile profile;
        profile.outputId = profile.microphoneId = profile.cameraId = Id(i);
        preferences->Add(profile);
    }
    Check(preferences->truncated == 1 && preferences->outputs.count == MaximumOutputs &&
              preferences->inputs.count == MaximumInputs && preferences->cameras.count == MaximumCameras,
          "loaded-profile union is bounded in each device class and reports overflow");
    Check(ValidInventoryPreferences(*preferences), "full bounded union is a valid protocol payload");
    preferences->truncated = 2;
    Check(!ValidInventoryPreferences(*preferences), "noncanonical truncation flag rejected");
    return checks;
}
