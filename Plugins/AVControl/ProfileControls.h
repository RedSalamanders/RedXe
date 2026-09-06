#pragma once
#include "AVControlProtocol.h"
#include <DxUi/DxUi.h>

namespace AVControl
{
struct ProfileCallbacks final
{
    void* context = nullptr;
    void (*apply)(void*, uint32_t index) noexcept = nullptr;
    HRESULT (*save)(void*, const Configuration&) noexcept = nullptr;
    void (*close)(void*) noexcept = nullptr;
    void (*invalidate)(void*) noexcept = nullptr;
};
// Temporary, retained chooser/editor inside one existing EmbeddedHost; never owns another surface or HWND.
class ProfileControls final
{
  public:
    struct RetainedState final
    {
        Profile draft;
        std::array<wchar_t, MaximumNameScalars * 2 + 1> name{};
        std::array<wchar_t, 192> error{};
        size_t editingIndex = MaximumProfiles;
        uint32_t mode = 0, page = 0;
    };
    ProfileControls(DxUi::Panel& parent, ProfileCallbacks callbacks);
    void SetConfiguration(const Configuration& configuration) noexcept;
    void SetInventory(const Inventory* inventory, uint64_t deviceRevision) noexcept;
    void Show() noexcept;
    void Hide() noexcept;
    void Back() noexcept;
    void Prepare(float width, float height);
    [[nodiscard]] bool IsEditing() const noexcept
    {
        return _mode == Mode::Editor;
    }
    [[nodiscard]] DxUi::Control* InitialFocus() const noexcept;
    [[nodiscard]] RetainedState CaptureState() const noexcept;
    void RestoreState(const RetainedState& state) noexcept;

  private:
    enum class Mode
    {
        Chooser,
        Definitions,
        Editor,
        CameraSetup
    };
    Mode _mode = Mode::Chooser;
    ProfileCallbacks _callbacks;
    Configuration _configuration;
    Profile _draft;
    const Inventory* _inventory = nullptr;
    uint64_t _deviceRevision = 0;
    size_t _editingIndex = MaximumProfiles;
    uint32_t _page = 0, _pageCount = 1;
    float _width = 0, _height = 0;
    bool _dirty = true, _draftDirty = true, _optionsDirty = true, _shown = false;
    std::array<wchar_t, 192> _error{};
    DxUi::Panel* _panel = nullptr;
    DxUi::Label* _title = nullptr;
    DxUi::Label* _message = nullptr;
    DxUi::Button* _back = nullptr;
    DxUi::Button* _remove = nullptr;
    DxUi::Button* _cameraSetup = nullptr;
    DxUi::PageIndicator* _indicator = nullptr;
    std::array<DxUi::Button*, MaximumProfiles> _profiles{};
    std::array<DxUi::Button*, 3> _footer{};
    std::array<DxUi::Control*, 8> _fields{};
    std::array<DxUi::Label*, 8> _captions{};
    DxUi::TextField* _name = nullptr;
    std::array<DxUi::ComboBox*, 3> _devices{};
    std::array<std::array<DeviceId, MaximumOutputs + 1>, 3> _deviceIds{};
    std::array<size_t, 3> _deviceCounts{};
    DxUi::ComboBox* _roles = nullptr;
    DxUi::Checkbox* _restore = nullptr;
    std::array<DxUi::Slider*, 2> _savedLevels{};
    void Invalidate() noexcept;
    void Error(std::wstring_view text) noexcept;
    void Activate(uint32_t index) noexcept;
    void Edit(size_t index) noexcept;
    void Save(bool remove) noexcept;
    void Footer(size_t index) noexcept;
    void FillDevices();
    void FillDraft();
};
} // namespace AVControl
