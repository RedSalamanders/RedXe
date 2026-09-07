#pragma once
#include "AVControlModel.h"
#include <algorithm>
#include <limits>
#include <span>

namespace AVControl
{
inline constexpr uint32_t UnreferencedDevice = (std::numeric_limits<uint32_t>::max)();

template <size_t Capacity> struct DevicePreferences final
{
    std::array<DeviceId, Capacity> ids{};
    uint32_t count = 0;

    [[nodiscard]] uint32_t Rank(const DeviceId& id) const noexcept
    {
        for (uint32_t i = 0; i < count; ++i)
            if (ids[i] == id)
                return i;
        return UnreferencedDevice;
    }
    bool Add(const DeviceId& id) noexcept
    {
        if (!id.length || Rank(id) != UnreferencedDevice)
            return true;
        if (count == Capacity)
            return false;
        ids[count++] = id;
        return true;
    }
    bool operator==(const DevicePreferences& other) const noexcept
    {
        return count == other.count && std::equal(ids.begin(), ids.begin() + count, other.ids.begin());
    }
};

// Bounded union of saved bindings in loaded widgets. Defaults and the selected camera are ranked ahead of this
// union by the backend. Overflow is explicit; it never silently changes a saved binding into another identity.
struct InventoryPreferences final
{
    DevicePreferences<MaximumOutputs> outputs;
    DevicePreferences<MaximumInputs> inputs;
    DevicePreferences<MaximumCameras> cameras;
    uint32_t truncated = 0;

    void Add(const Profile& profile) noexcept
    {
        const bool output = outputs.Add(profile.outputId);
        const bool input = inputs.Add(profile.microphoneId);
        const bool camera = cameras.Add(profile.cameraId);
        if (!output || !input || !camera)
            truncated = 1;
    }
    bool operator==(const InventoryPreferences&) const noexcept = default;
};

// One-pass bounded admission: late defaults/references displace ordinary devices, and equal priorities keep
// enumeration order. Membership is finalized before opening any endpoint-volume subscription.
template <typename Descriptor, size_t Capacity> class RankedInventory final
{
  public:
    RankedInventory(std::array<Descriptor, Capacity>& rows, uint32_t& count) noexcept : _rows(rows), _count(count)
    {
        _count = 0;
    }
    void Offer(const Descriptor& row, uint32_t rank) noexcept
    {
        for (uint32_t i = 0; i < _count; ++i)
            if (_rows[i].id == row.id)
                return;
        if (_count < Capacity)
        {
            _ranks[_count] = rank;
            _rows[_count++] = row;
            return;
        }
        _truncated = true;
        uint32_t worst = 0;
        for (uint32_t i = 1; i < _count; ++i)
            if (_ranks[i] >= _ranks[worst])
                worst = i;
        if (rank < _ranks[worst])
        {
            _ranks[worst] = rank;
            _rows[worst] = row;
        }
    }
    [[nodiscard]] bool Truncated() const noexcept
    {
        return _truncated;
    }

  private:
    std::array<Descriptor, Capacity>& _rows;
    uint32_t& _count;
    std::array<uint32_t, Capacity> _ranks{};
    bool _truncated = false;
};

template <size_t Capacity>
uint32_t InventoryRank(const DeviceId& id, std::span<const DeviceId> defaults,
                       const DevicePreferences<Capacity>& references) noexcept
{
    for (uint32_t i = 0; i < defaults.size(); ++i)
        if (defaults[i] == id)
            return i;
    const uint32_t reference = references.Rank(id);
    return reference == UnreferencedDevice ? reference : static_cast<uint32_t>(defaults.size()) + reference;
}
} // namespace AVControl
