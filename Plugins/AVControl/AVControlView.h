#pragma once
#include "AVControlLayout.h"
#include "AVControlModel.h"
#include "PlugInterfaces/Widget.h"
#include "ProfileControls.h"
#include <DxUi/Embedded.h>

namespace AVControl
{
enum class ViewCommandKind : uint32_t
{
    SetMuted,
    SetCameraEnabled,
    SetLevel,
    OpenProfiles,
    ApplyProfile,
    CloseProfiles
};
struct ViewCommand final
{
    ViewCommandKind kind{};
    DeviceKind device{};
    DeviceId endpointId;
    uint64_t generation = 0;
    uint64_t revision = 0;
    uint32_t value = 0;
};
struct ViewCallbacks final
{
    void* context = nullptr;
    void (*command)(void*, const ViewCommand&) noexcept = nullptr;
    void (*requestFrame)(void*) noexcept = nullptr;
    HRESULT (*saveProfiles)(void*, const Configuration&) noexcept = nullptr;
};

// Retained public DxUI controls. One instance per tile/raised view; the caller owns the confirmed shared model.
class LiveView final
{
  public:
    HRESULT Attach(std::shared_ptr<DxUi::GraphicsDevice> graphics, ViewCallbacks callbacks) noexcept;
    void Detach() noexcept;
    void SetVisible(bool visible) noexcept;
    void SetState(const ConfirmedState& state, std::wstring_view profileName, uint32_t pendingMask) noexcept;
    void SetAppearance(const RedXeAppearance& appearance) noexcept;
    HRESULT Prepare(uint32_t width, uint32_t height, float dpi) noexcept;
    HRESULT Composite(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport) noexcept;
    bool Pointer(const DxUi::PointerEvent& event) noexcept;
    bool Key(UINT key, bool down, UINT modifiers = 0) noexcept;
    bool Character(wchar_t character, UINT modifiers = 0) noexcept;
    HRESULT ReadText(RedXeTextState& state) noexcept;
    HRESULT ApplyText(uint32_t action, const RedXeTextState& state) noexcept;
    HRESULT CancelText(uint64_t focusId) noexcept;
    HRESULT HitText(uint64_t revision, float x, float y, uint32_t& index) noexcept;
    HRESULT TextRange(uint64_t revision, uint32_t start, uint32_t end, RedXeTextRectangle& bounds,
                      BOOL& clipped) noexcept;
    HRESULT ConnectAccessibility(const RedXeAccessibilityPlacement& placement, IRedXeAccessibilitySite* site,
                                 IRawElementProviderFragmentRoot** result) noexcept;
    HRESULT UpdateAccessibility(const RedXeAccessibilityPlacement& placement) noexcept;
    void DisconnectAccessibility() noexcept;
    void LoseTextFocus() noexcept
    {
        _view.CancelTextInput();
    }
    [[nodiscard]] bool TabBoundary() const noexcept
    {
        return _tabBoundary;
    }
    void Cancel() noexcept;
    void SetProfiles(const Configuration& configuration, const Inventory* inventory, uint64_t deviceRevision) noexcept;
    void ShowProfiles() noexcept;
    void CloseProfiles() noexcept;
    [[nodiscard]] bool ProfilesVisible() const noexcept
    {
        return _profilesVisible;
    }
    [[nodiscard]] DxUi::EmbeddedStatistics Statistics() const noexcept
    {
        return _view.GetStatistics();
    }
    [[nodiscard]] DxUi::ControlHost& Controls() noexcept
    {
        return _view.Controls();
    }
    [[nodiscard]] const LiveLayout& Layout() const noexcept
    {
        return _layout;
    }

  private:
    DxUi::EmbeddedHost _view;
    ViewCallbacks _callbacks;
    ConfirmedState _state;
    std::array<wchar_t, 128> _profileName{};
    uint32_t _pendingMask = 0;
    uint32_t _width = 0, _height = 0;
    float _dpi = 96;
    uint64_t _layoutRevision = 0;
    bool _modelDirty = true;
    RedXeAppearance _appearance;
    bool _themeDirty = true;
    bool _ready = false;
    bool _visible = false;
    bool _profilesVisible = false, _profileFocusPending = false;
    bool _tabBoundary = false;
    Configuration _configuration;
    const Inventory* _inventory = nullptr;
    uint64_t _deviceRevision = 0;
    DxUi::Panel* _livePanel = nullptr;
    std::unique_ptr<ProfileControls> _profileControls;
    std::optional<ProfileControls::RetainedState> _retainedProfile;
    LiveLayout _layout;
    std::array<LevelGesture, 2> _gestures;
    DxUi::Label* _heading = nullptr;
    DxUi::Button* _profile = nullptr;
    DxUi::Label* _profileCaption = nullptr;
    uint64_t _accessibilityIdentity = 0;
    std::array<DxUi::Toggle*, 3> _toggles{};
    std::array<DxUi::Label*, 3> _toggleIcons{}, _toggleNames{};
    std::array<DxUi::CardPanel*, 2> _levelCards{};
    std::array<DxUi::Label*, 2> _levelIcons{}, _levelValues{};
    std::array<DxUi::Slider*, 2> _sliders{};
    void Build();
    void ApplyTheme();
    void Arrange();
    void Refresh();
    void Toggle(size_t index) noexcept;
    void SliderChanged(size_t index, DxUi::SliderChange change) noexcept;
    void Emit(ViewCommand command) noexcept;
    [[nodiscard]] const Endpoint& Audio(size_t index) const noexcept
    {
        return index ? _state.microphone : _state.output;
    }
    [[nodiscard]] const wchar_t* DeviceName(size_t index) const noexcept;
};
} // namespace AVControl
