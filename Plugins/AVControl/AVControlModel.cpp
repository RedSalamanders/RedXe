#include "AVControlModel.h"

#include <algorithm>
#include <cstring>
#include <yyjson.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace AVControl
{
namespace
{
using JsonDocument = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using MutableDocument = wil::unique_any<yyjson_mut_doc*, decltype(&yyjson_mut_doc_free), yyjson_mut_doc_free>;
using JsonBuffer = wil::unique_any<char*, decltype(&free), free>;

bool ValidUtf8(std::string_view value, size_t maximumScalars, bool rejectControls) noexcept
{
    size_t scalars = 0;
    for (size_t i = 0; i < value.size();)
    {
        const uint8_t first = static_cast<uint8_t>(value[i++]);
        uint32_t scalar = first;
        size_t trailing = 0;
        uint32_t minimum = 0;
        if (first >= 0xC2 && first <= 0xDF)
        {
            trailing = 1;
            scalar = first & 0x1F;
            minimum = 0x80;
        }
        else if (first >= 0xE0 && first <= 0xEF)
        {
            trailing = 2;
            scalar = first & 0x0F;
            minimum = 0x800;
        }
        else if (first >= 0xF0 && first <= 0xF4)
        {
            trailing = 3;
            scalar = first & 0x07;
            minimum = 0x10000;
        }
        else if (first >= 0x80)
            return false;
        if (trailing > value.size() - i)
            return false;
        while (trailing--)
        {
            const uint8_t next = static_cast<uint8_t>(value[i++]);
            if ((next & 0xC0) != 0x80)
                return false;
            scalar = (scalar << 6U) | (next & 0x3F);
        }
        if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF) || scalar == 0)
            return false;
        if (rejectControls && (scalar < 0x20 || (scalar >= 0x7F && scalar <= 0x9F)))
            return false;
        if (++scalars > maximumScalars)
            return false;
    }
    return scalars != 0;
}

std::string_view String(yyjson_val* value) noexcept
{
    return yyjson_is_str(value) ? std::string_view{yyjson_get_str(value), yyjson_get_len(value)} : std::string_view{};
}

bool ValidIdentifier(std::string_view text) noexcept
{
    if (text.empty() || text.size() > 32)
        return false;
    for (const char ch : text)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
              ch == '_'))
            return false;
    return true;
}

HRESULT Fail(ConfigurationError value, ConfigurationError* error) noexcept
{
    if (error)
        *error = value;
    return E_INVALIDARG;
}

ConfigurationError ValidateProfile(const Profile& profile) noexcept
{
    if (!ValidIdentifier(profile.id.View()))
        return ConfigurationError::Identifier;
    if (!ValidUtf8(profile.name.View(), MaximumNameScalars, true))
        return ConfigurationError::Name;
    for (const DeviceId* id : {&profile.outputId, &profile.microphoneId, &profile.cameraId})
        if (!ValidUtf8(id->View(), MaximumDeviceIdBytes, false))
            return ConfigurationError::DeviceIdentifier;
    if (profile.outputLevel > 100 || profile.microphoneLevel > 100)
        return ConfigurationError::Level;
    if (profile.audioRoles != AudioRoles::All && profile.audioRoles != AudioRoles::Communications)
        return ConfigurationError::Roles;
    return ConfigurationError::None;
}
} // namespace

bool SameProfileId(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
        return false;
    const auto fold = [](char ch) noexcept
    { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch; };
    for (size_t i = 0; i < left.size(); ++i)
        if (fold(left[i]) != fold(right[i]))
            return false;
    return true;
}

HRESULT ParseConfiguration(std::string_view json, Configuration& destination, ConfigurationError* error) noexcept
{
    if (error)
        *error = ConfigurationError::None;
    if (json.empty() || json.size() > MaximumSettingsBytes)
        return Fail(ConfigurationError::Capacity, error);
    JsonDocument document{yyjson_read(json.data(), json.size(), 0)};
    if (!document)
        return Fail(ConfigurationError::Json, error);
    yyjson_val* root = yyjson_doc_get_root(document.get());
    if (!yyjson_is_obj(root) || yyjson_obj_size(root) != 1)
        return Fail(ConfigurationError::Shape, error);
    yyjson_val* profiles = yyjson_obj_get(root, "profiles");
    if (!yyjson_is_arr(profiles))
        return Fail(ConfigurationError::Shape, error);
    if (yyjson_arr_size(profiles) > MaximumProfiles)
        return Fail(ConfigurationError::Capacity, error);
    Configuration staged;
    size_t index = 0, maximum = 0;
    yyjson_val* value = nullptr;
    yyjson_arr_foreach(profiles, index, maximum, value)
    {
        if (!yyjson_is_obj(value) || yyjson_obj_size(value) != 9)
            return Fail(ConfigurationError::Shape, error);
        constexpr std::array keys{"id",         "name",          "outputId",    "microphoneId",   "cameraId",
                                  "audioRoles", "restoreLevels", "outputLevel", "microphoneLevel"};
        uint32_t seen = 0;
        size_t memberIndex = 0, memberCount = 0;
        yyjson_val* key = nullptr;
        yyjson_val* member = nullptr;
        yyjson_obj_foreach(value, memberIndex, memberCount, key, member)
        {
            const auto found = std::find(keys.begin(), keys.end(), String(key));
            if (found == keys.end())
                return Fail(ConfigurationError::Shape, error);
            const uint32_t mask = 1U << static_cast<uint32_t>(found - keys.begin());
            if ((seen & mask) != 0)
                return Fail(ConfigurationError::Shape, error);
            seen |= mask;
        }
        Profile& profile = staged.profiles[index];
        if (!profile.id.Assign(String(yyjson_obj_get(value, "id"))))
            return Fail(ConfigurationError::Identifier, error);
        if (!profile.name.Assign(String(yyjson_obj_get(value, "name"))))
            return Fail(ConfigurationError::Name, error);
        if (!profile.outputId.Assign(String(yyjson_obj_get(value, "outputId"))) ||
            !profile.microphoneId.Assign(String(yyjson_obj_get(value, "microphoneId"))) ||
            !profile.cameraId.Assign(String(yyjson_obj_get(value, "cameraId"))))
            return Fail(ConfigurationError::DeviceIdentifier, error);
        const std::string_view roles = String(yyjson_obj_get(value, "audioRoles"));
        if (roles != "all" && roles != "communications")
            return Fail(ConfigurationError::Roles, error);
        profile.audioRoles = roles == "all" ? AudioRoles::All : AudioRoles::Communications;
        yyjson_val* restore = yyjson_obj_get(value, "restoreLevels");
        yyjson_val* outputLevel = yyjson_obj_get(value, "outputLevel");
        yyjson_val* inputLevel = yyjson_obj_get(value, "microphoneLevel");
        if (!yyjson_is_bool(restore))
            return Fail(ConfigurationError::Shape, error);
        if (!yyjson_is_uint(outputLevel) || !yyjson_is_uint(inputLevel) || yyjson_get_uint(outputLevel) > 100 ||
            yyjson_get_uint(inputLevel) > 100)
            return Fail(ConfigurationError::Level, error);
        profile.restoreLevels = yyjson_get_bool(restore);
        profile.outputLevel = static_cast<uint32_t>(yyjson_get_uint(outputLevel));
        profile.microphoneLevel = static_cast<uint32_t>(yyjson_get_uint(inputLevel));
        const ConfigurationError validation = ValidateProfile(profile);
        if (validation != ConfigurationError::None)
            return Fail(validation, error);
        for (size_t previous = 0; previous < index; ++previous)
            if (SameProfileId(profile.id.View(), staged.profiles[previous].id.View()))
                return Fail(ConfigurationError::DuplicateProfile, error);
        ++staged.count;
    }
    destination = staged;
    return S_OK;
}

HRESULT SerializeConfiguration(const Configuration& configuration, char* destination, size_t capacity,
                               uint32_t& writtenBytes) noexcept
{
    writtenBytes = 0;
    if (!destination)
        return E_POINTER;
    if (configuration.count > MaximumProfiles)
        return E_INVALIDARG;
    MutableDocument document{yyjson_mut_doc_new(nullptr)};
    if (!document)
        return E_OUTOFMEMORY;
    yyjson_mut_val* root = yyjson_mut_obj(document.get());
    yyjson_mut_val* profiles = yyjson_mut_arr(document.get());
    if (!root || !profiles || !yyjson_mut_obj_add_val(document.get(), root, "profiles", profiles))
        return E_OUTOFMEMORY;
    yyjson_mut_doc_set_root(document.get(), root);
    for (size_t i = 0; i < configuration.count; ++i)
    {
        const Profile& profile = configuration.profiles[i];
        if (ValidateProfile(profile) != ConfigurationError::None)
            return E_INVALIDARG;
        for (size_t j = 0; j < i; ++j)
            if (SameProfileId(profile.id.View(), configuration.profiles[j].id.View()))
                return E_INVALIDARG;
        yyjson_mut_val* object = yyjson_mut_obj(document.get());
        if (!object || !yyjson_mut_arr_append(profiles, object))
            return E_OUTOFMEMORY;
        const auto addString = [&](const char* key, std::string_view text) noexcept
        { return yyjson_mut_obj_add_strncpy(document.get(), object, key, text.data(), text.size()); };
        if (!addString("id", profile.id.View()) || !addString("name", profile.name.View()) ||
            !addString("outputId", profile.outputId.View()) ||
            !addString("microphoneId", profile.microphoneId.View()) ||
            !addString("cameraId", profile.cameraId.View()) ||
            !addString("audioRoles", profile.audioRoles == AudioRoles::All ? "all" : "communications") ||
            !yyjson_mut_obj_add_bool(document.get(), object, "restoreLevels", profile.restoreLevels) ||
            !yyjson_mut_obj_add_uint(document.get(), object, "outputLevel", profile.outputLevel) ||
            !yyjson_mut_obj_add_uint(document.get(), object, "microphoneLevel", profile.microphoneLevel))
            return E_OUTOFMEMORY;
    }
    size_t bytes = 0;
    JsonBuffer json{yyjson_mut_write(document.get(), 0, &bytes)};
    if (!json)
        return E_OUTOFMEMORY;
    if (bytes > MaximumSettingsBytes || bytes > capacity)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    std::memcpy(destination, json.get(), bytes);
    writtenBytes = static_cast<uint32_t>(bytes);
    return S_OK;
}

ProfileMatch MatchProfile(const Profile& profile, const ConfirmedState& state) noexcept
{
    if (state.output.availability != Availability::Ready || state.microphone.availability != Availability::Ready ||
        state.camera.availability != Availability::Ready || !(state.camera.sourceId == profile.cameraId))
        return ProfileMatch::Custom;
    const size_t firstRole = profile.audioRoles == AudioRoles::Communications ? 2 : 0;
    for (size_t role = firstRole; role < 3; ++role)
        if (!(state.outputDefaults[role] == profile.outputId) || !(state.inputDefaults[role] == profile.microphoneId))
            return ProfileMatch::Custom;
    if (!(state.output.id == profile.outputId) || !(state.microphone.id == profile.microphoneId))
        return ProfileMatch::Custom;
    if (profile.restoreLevels &&
        (profile.outputLevel != state.output.level || profile.microphoneLevel != state.microphone.level))
        return ProfileMatch::Adjusted;
    return ProfileMatch::Active;
}

bool LevelGesture::Begin(const Endpoint& endpoint, uint64_t layoutRevision) noexcept
{
    Cancel();
    if (endpoint.availability != Availability::Ready || endpoint.id.View().empty() || endpoint.level > 100)
        return false;
    _id = endpoint.id;
    _generation = endpoint.generation;
    _levelRevision = endpoint.levelRevision;
    _layoutRevision = layoutRevision;
    _initial = _draft = endpoint.level;
    _active = true;
    return true;
}
bool LevelGesture::Preview(uint32_t level) noexcept
{
    if (!_active || level > 100)
        return false;
    _draft = level;
    return true;
}
bool LevelGesture::Commit(const Endpoint& endpoint, uint64_t layoutRevision, uint32_t& level) noexcept
{
    const bool coherent = _active && endpoint.availability == Availability::Ready && endpoint.id == _id &&
                          endpoint.generation == _generation && endpoint.levelRevision == _levelRevision &&
                          layoutRevision == _layoutRevision;
    const uint32_t draft = _draft;
    Cancel();
    if (coherent)
        level = draft;
    return coherent;
}
void LevelGesture::Cancel() noexcept
{
    _active = false;
    _draft = _initial;
}
uint32_t StepLevel(uint32_t level, int32_t delta) noexcept
{
    return static_cast<uint32_t>(
        std::clamp(static_cast<int64_t>(std::min(level, 100U)) + delta, int64_t{0}, int64_t{100}));
}
} // namespace AVControl
