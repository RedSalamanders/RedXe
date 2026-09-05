#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <windows.h>

namespace AVControl
{
inline constexpr size_t MaximumProfiles = 4;
inline constexpr size_t MaximumSettingsBytes = 4096;
inline constexpr size_t MaximumDeviceIdBytes = 1024;
inline constexpr size_t MaximumNameScalars = 48;
inline constexpr size_t MaximumOutputs = 32;
inline constexpr size_t MaximumInputs = 32;
inline constexpr size_t MaximumCameras = 16;

template <size_t Capacity> struct Text final
{
    std::array<char, Capacity + 1> bytes{};
    uint32_t length = 0;

    [[nodiscard]] std::string_view View() const noexcept
    {
        return length <= Capacity ? std::string_view{bytes.data(), length} : std::string_view{};
    }
    [[nodiscard]] bool Assign(std::string_view value) noexcept
    {
        if (value.size() > Capacity)
            return false;
        for (size_t i = 0; i < value.size(); ++i)
            bytes[i] = value[i];
        bytes[value.size()] = '\0';
        length = static_cast<uint32_t>(value.size());
        return true;
    }
    bool operator==(const Text& other) const noexcept { return View() == other.View(); }
};

using DeviceId = Text<MaximumDeviceIdBytes>;
enum class AudioRoles : uint32_t { All, Communications };
enum class DeviceKind : uint32_t { Output, Microphone, Camera };
enum class Availability : uint32_t { Unknown, Ready, Missing, AccessDenied, Busy, Unsupported, Failed };

struct Profile final
{
    Text<32> id;
    Text<MaximumNameScalars * 4> name;
    DeviceId outputId;
    DeviceId microphoneId;
    DeviceId cameraId;
    AudioRoles audioRoles = AudioRoles::All;
    bool restoreLevels = false;
    uint32_t outputLevel = 0;
    uint32_t microphoneLevel = 0;
};

struct Configuration final
{
    std::array<Profile, MaximumProfiles> profiles{};
    uint32_t count = 0;
};

enum class ConfigurationError : uint32_t
{
    None, Json, Shape, Capacity, Identifier, Name, DeviceIdentifier, DuplicateProfile, Level, Roles
};

// Strict, transactional parsing. On failure destination remains unchanged. IDs are never truncated or repaired.
[[nodiscard]] HRESULT ParseConfiguration(std::string_view json, Configuration& destination,
                                         ConfigurationError* error = nullptr) noexcept;
// Writes a complete compact object without a terminator; failure publishes no partial output.
[[nodiscard]] HRESULT SerializeConfiguration(const Configuration& configuration, char* destination, size_t capacity,
                                             uint32_t& writtenBytes) noexcept;
[[nodiscard]] bool SameProfileId(std::string_view left, std::string_view right) noexcept;

struct Endpoint final
{
    DeviceId id;
    std::array<wchar_t, 257> name{};
    Availability availability = Availability::Unknown;
    uint32_t level = 0;
    bool muted = false;
    // A device generation changes on replacement/disappearance, not on an unrelated volume notification.
    uint64_t generation = 0;
    uint64_t levelRevision = 0;
    uint64_t muteRevision = 0;
};

struct CameraState final
{
    DeviceId sourceId;
    Availability availability = Availability::Unknown;
    bool enabled = false;
    uint64_t revision = 0;
};

struct ConfirmedState final
{
    Endpoint output;
    Endpoint microphone;
    CameraState camera;
    std::array<DeviceId, 3> outputDefaults{};
    std::array<DeviceId, 3> inputDefaults{};
    uint64_t revision = 0;
};

enum class ProfileMatch : uint32_t { Custom, Active, Adjusted };
[[nodiscard]] ProfileMatch MatchProfile(const Profile& profile, const ConfirmedState& state) noexcept;

// Widget-local preview state. A geometry/device/level revision invalidates a gesture before it can write a new endpoint.
class LevelGesture final
{
  public:
    [[nodiscard]] bool Begin(const Endpoint& endpoint, uint64_t layoutRevision) noexcept;
    [[nodiscard]] bool Preview(uint32_t level) noexcept;
    [[nodiscard]] bool Commit(const Endpoint& endpoint, uint64_t layoutRevision, uint32_t& level) noexcept;
    void Cancel() noexcept;
    [[nodiscard]] bool Active() const noexcept { return _active; }
    [[nodiscard]] uint32_t Draft() const noexcept { return _draft; }
  private:
    DeviceId _id;
    uint64_t _generation = 0;
    uint64_t _levelRevision = 0;
    uint64_t _layoutRevision = 0;
    uint32_t _initial = 0;
    uint32_t _draft = 0;
    bool _active = false;
};
[[nodiscard]] uint32_t StepLevel(uint32_t level, int32_t delta) noexcept;
} // namespace AVControl
