#pragma once
#include "AVControlModel.h"
#include "InventorySelection.h"
#include <type_traits>

namespace AVControl
{
// Private, same-build parent/helper protocol. No pointers, handles, strings of unbounded length, or C++ ownership
// cross the mapping. The mapping and events are unnamed, inherited only by the owned child through a handle list.
inline constexpr uint32_t BrokerProtocolVersion = 3;
inline constexpr uint32_t BrokerMagic = 0x56415852;
enum class BrokerOperation : uint32_t
{
    Observe, SetMute, SetLevel, SetDefault, SetCameraSource, SetCameraEnabled, SuspendObservation,
    // Available only to an explicitly synthetic helper; never accepted by the hardware backend.
    FixtureHang, FixtureExit, FixtureMalformedReply
};
enum BackendCapability : uint32_t
{
    CapabilityAudioLevels = 1U << 0,
    CapabilityAudioDefaults = 1U << 1,
    CapabilityCameraRoute = 1U << 2
};
struct CameraDescriptor final
{
    DeviceId id;
    std::array<wchar_t, 257> name{};
    Availability availability = Availability::Unknown;
};
struct Inventory final
{
    ConfirmedState state;
    std::array<Endpoint, MaximumOutputs> outputs{};
    std::array<Endpoint, MaximumInputs> inputs{};
    std::array<CameraDescriptor, MaximumCameras> cameras{};
    std::array<uint64_t, 3> outputRoleRevisions{};
    std::array<uint64_t, 3> inputRoleRevisions{};
    uint32_t outputCount = 0, inputCount = 0, cameraCount = 0;
    uint32_t capabilities = 0;
    bool truncated = false;
    bool cameraRequiresHelper = false;
};
struct BrokerCommand final
{
    BrokerOperation operation = BrokerOperation::Observe;
    DeviceKind device = DeviceKind::Output;
    DeviceId id;
    uint32_t value = 0;
    uint32_t role = 0;
    uint64_t expectedGeneration = 0;
    uint64_t expectedRevision = 0;
};
struct BrokerReply final
{
    HRESULT result = E_PENDING;
    Inventory inventory;
};
struct BrokerShared final
{
    uint32_t magic = BrokerMagic;
    uint32_t version = BrokerProtocolVersion;
    uint32_t sizeBytes = sizeof(BrokerShared);
    uint32_t synthetic = 0;
    uint32_t requestSequence = 0;
    uint32_t replySequence = 0;
    InventoryPreferences preferences;
    BrokerCommand command;
    BrokerReply reply;
};
static_assert(std::is_trivially_copyable_v<BrokerShared>);
static_assert(sizeof(BrokerShared) < 256 * 1024);
} // namespace AVControl
