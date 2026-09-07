#pragma once
#include "AVControlProtocol.h"
#include <cstring>

namespace AVControl
{
// Validate the complete fixed-capacity reply before copying or rendering it. In particular, never interpret
// an invalid bool representation or a helper-controlled length/unterminated string as trusted C++ state.
inline bool ValidProtocolBool(const bool& value) noexcept
{
    static_assert(sizeof(bool) == 1);
    unsigned char byte = 0;
    std::memcpy(&byte, &value, 1);
    return byte <= 1;
}
inline bool ValidProtocolId(const DeviceId& id, bool required = false) noexcept
{
    if (id.length > MaximumDeviceIdBytes || (required && !id.length) || id.bytes[id.length] != 0)
        return false;
    if (!id.length)
        return true;
    if (std::memchr(id.bytes.data(), 0, id.length))
        return false;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, id.bytes.data(), static_cast<int>(id.length), nullptr,
                               0) > 0;
}
inline bool ValidProtocolName(const std::array<wchar_t, 257>& name) noexcept
{
    const auto size = wcsnlen_s(name.data(), name.size());
    return size < name.size() &&
           (!size || WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(size), nullptr, 0,
                                         nullptr, nullptr) > 0);
}
inline bool ValidInventoryPreferences(const InventoryPreferences& preferences) noexcept
{
    const auto valid = [](const auto& rows) noexcept
    {
        if (rows.count > rows.ids.size())
            return false;
        for (uint32_t i = 0; i < rows.count; ++i)
        {
            if (!ValidProtocolId(rows.ids[i], true))
                return false;
            for (uint32_t j = 0; j < i; ++j)
                if (rows.ids[i] == rows.ids[j])
                    return false;
        }
        return true;
    };
    return preferences.truncated <= 1 && valid(preferences.outputs) && valid(preferences.inputs) &&
           valid(preferences.cameras);
}
inline bool ValidProtocolAvailability(Availability value) noexcept
{
    return static_cast<uint32_t>(value) <= static_cast<uint32_t>(Availability::Failed);
}
inline bool ValidProtocolEndpoint(const Endpoint& endpoint, bool listed) noexcept
{
    if (!ValidProtocolAvailability(endpoint.availability) || !ValidProtocolBool(endpoint.muted) ||
        endpoint.level > 100 || !ValidProtocolId(endpoint.id, listed || endpoint.availability == Availability::Ready) ||
        !ValidProtocolName(endpoint.name))
        return false;
    return endpoint.availability != Availability::Ready ||
           (endpoint.generation && endpoint.levelRevision && endpoint.muteRevision);
}
inline bool ValidInventoryPayload(const Inventory& inventory) noexcept
{
    if (inventory.outputCount > MaximumOutputs || inventory.inputCount > MaximumInputs ||
        inventory.cameraCount > MaximumCameras ||
        inventory.capabilities & ~(CapabilityAudioLevels | CapabilityAudioDefaults | CapabilityCameraRoute) ||
        !ValidProtocolBool(inventory.truncated) || !ValidProtocolBool(inventory.cameraRequiresHelper) ||
        !ValidProtocolEndpoint(inventory.state.output, false) ||
        !ValidProtocolEndpoint(inventory.state.microphone, false))
        return false;
    const auto& camera = inventory.state.camera;
    if (!ValidProtocolAvailability(camera.availability) || !ValidProtocolBool(camera.enabled) ||
        !ValidProtocolId(camera.sourceId, camera.availability == Availability::Ready) ||
        (camera.availability == Availability::Ready && !camera.revision))
        return false;
    for (uint32_t role = 0; role < 3; ++role)
        if (!ValidProtocolId(inventory.state.outputDefaults[role]) ||
            !ValidProtocolId(inventory.state.inputDefaults[role]))
            return false;
    for (uint32_t flow = 0; flow < 2; ++flow)
    {
        const auto& rows = flow ? inventory.inputs : inventory.outputs;
        const auto count = flow ? inventory.inputCount : inventory.outputCount;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!ValidProtocolEndpoint(rows[i], true))
                return false;
            for (uint32_t j = 0; j < i; ++j)
                if (rows[i].id == rows[j].id)
                    return false;
        }
    }
    for (uint32_t i = 0; i < inventory.cameraCount; ++i)
    {
        const auto& row = inventory.cameras[i];
        if (!ValidProtocolId(row.id, true) || !ValidProtocolName(row.name) ||
            !ValidProtocolAvailability(row.availability))
            return false;
        for (uint32_t j = 0; j < i; ++j)
            if (row.id == inventory.cameras[j].id)
                return false;
    }
    return true;
}
} // namespace AVControl
