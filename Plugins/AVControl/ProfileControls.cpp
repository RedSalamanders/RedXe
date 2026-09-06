#include "ProfileControls.h"
#include <algorithm>
#include <cwchar>

namespace AVControl
{
namespace
{
std::wstring Wide(std::string_view utf8)
{
    if (utf8.empty())
        return {};
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring result(static_cast<size_t>(length), 0);
    (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), result.data(),
                              length);
    return result;
}
void Bounds(DxUi::Control* control, float x, float y, float width, float height) noexcept
{
    control->SetBounds(D2D1::RectF(x, y, x + width, y + height));
}
struct CameraSetupStep final
{
    std::wstring_view shortText, detail;
};
constexpr std::array cameraSetupSteps{
    CameraSetupStep{
        L"RedXe Camera needs Windows 11.",
        L"RedXe Camera requires Windows 11. Output volume and microphone controls also work on Windows 10."},
    CameraSetupStep{L"Install the RedXe Camera component.",
                    L"Install the native RedXe Camera Release package with the supplied installer. Machine "
                    L"installation needs administrator approval. Close apps using RedXe Camera before updating it."},
    CameraSetupStep{L"Register it for your Windows account.",
                    L"In a non-elevated PowerShell terminal for your account, run setup-camera.ps1 -Action Register. "
                    L"This creates the persistent RedXe Camera device without opening your webcam. The package setup "
                    L"guide covers installation and removal."},
    CameraSetupStep{L"Restart RedXe after installation.",
                    L"Restart RedXe after installation so the plugin checks the new component. Missing setup or denied "
                    L"camera permission leaves camera control unavailable; audio controls remain usable."},
    CameraSetupStep{L"Choose RedXe Camera in each calling app.",
                    L"In each calling or recording app, select RedXe Camera once. Windows may append Windows Virtual "
                    L"Camera to its name. Selecting a physical webcam directly bypasses this plugin's camera control."},
    CameraSetupStep{L"Choose a webcam in an AV profile. Off shows a neutral frame.",
                    L"Define an AV profile with the physical webcam, microphone and output you want. Apply it, then "
                    L"turn Camera On. Capture starts when an app requests frames. Off releases the webcam and supplies "
                    L"neutral frames; it does not disable another app's direct webcam access."}};
} // namespace
ProfileControls::ProfileControls(DxUi::Panel& parent, ProfileCallbacks callbacks) : _callbacks(callbacks)
{
    _panel = parent.AddChild<DxUi::Panel>();
    _panel->SetVisible(false);
    _back = _panel->AddChild<DxUi::Button>(L"Back");
    _back->SetOnClick([this] { Back(); });
    _back->SetAccessibleName(L"Back to the previous AV view");
    _title = _panel->AddChild<DxUi::Label>(L"Profiles");
    _title->SetFontRole(DxUi::FontRole::BodyLarge);
    _remove = _panel->AddChild<DxUi::Button>(L"Delete");
    _remove->SetOnClick([this] { Save(true); });
    _remove->SetAccessibleName(L"Delete this profile definition without changing devices");
    _message = _panel->AddChild<DxUi::Label>();
    _message->SetMultiline(true);
    _cameraSetup = _panel->AddChild<DxUi::Button>(L"Camera setup");
    _cameraSetup->SetAccessibleName(L"Camera setup guide");
    _cameraSetup->SetAccessibleAutomationId(L"av.camera.setup");
    _cameraSetup->SetOnClick(
        [this]
        {
            _mode = Mode::CameraSetup;
            _page = 0;
            _error[0] = 0;
            Invalidate();
        });
    _indicator = _panel->AddChild<DxUi::PageIndicator>();
    _indicator->SetAccessibleAutomationId(L"av.page.indicator");
    _indicator->SetOnSelected(
        [this](uint32_t index)
        {
            _page = index;
            Invalidate();
        });
    for (uint32_t i = 0; i < MaximumProfiles; ++i)
    {
        _profiles[i] = _panel->AddChild<DxUi::Button>();
        _profiles[i]->SetOnClick([this, i] { Activate(i); });
    }
    for (size_t i = 0; i < 3; ++i)
    {
        _footer[i] = _panel->AddChild<DxUi::Button>();
        _footer[i]->SetOnClick([this, i] { Footer(i); });
    }
    constexpr std::array captions{L"Profile name",        L"Audio output",         L"Microphone",
                                  L"Camera source",       L"Audio role scope",     L"Optional saved levels",
                                  L"Saved output volume", L"Saved microphone gain"};
    for (size_t i = 0; i < _captions.size(); ++i)
    {
        _captions[i] = _panel->AddChild<DxUi::Label>(captions[i]);
        _captions[i]->SetFontRole(DxUi::FontRole::BodyLarge);
    }
    _name = _panel->AddChild<DxUi::TextField>();
    _fields[0] = _name;
    _name->SetClearButtonEnabled(false);
    _name->SetAccessibleName(L"Profile name, up to 48 characters");
    _name->SetAccessibleAutomationId(L"av.profile.name");
    _name->SetOnTextChanged(
        [this](std::wstring_view text)
        {
            // A transient IME draft may be incomplete Unicode. Bound its storage here and validate scalar count at
            // Save.
            if (text.size() > MaximumNameScalars * 2)
            {
                size_t count = MaximumNameScalars * 2;
                if (text[count - 1] >= 0xd800 && text[count - 1] <= 0xdbff)
                    --count;
                try
                {
                    _name->SetText(std::wstring(text.substr(0, count)));
                }
                catch (...)
                {
                    Error(L"Unable to update the profile name.");
                }
            }
        });
    for (size_t i = 0; i < 3; ++i)
    {
        auto* combo = _devices[i] = _panel->AddChild<DxUi::ComboBox>();
        _fields[i + 1] = combo;
        combo->SetMinimumPopupItemHeight(48.0f);
        combo->SetEditable(false);
        combo->SetMaxVisibleItems(5);
        combo->SetAccessibleName(captions[i + 1]);
        combo->SetOnSelectionChanged(
            [this, i](size_t selected)
            {
                if (selected >= _deviceCounts[i])
                    return;
                auto& id = i == 0 ? _draft.outputId : i == 1 ? _draft.microphoneId : _draft.cameraId;
                id = _deviceIds[i][selected];
            });
    }
    _roles = _panel->AddChild<DxUi::ComboBox>();
    _fields[4] = _roles;
    _roles->SetMinimumPopupItemHeight(48.0f);
    _roles->SetItems({{L"all", L"All Windows roles"}, {L"communications", L"Communications only"}});
    _roles->SetAccessibleName(L"Audio role scope");
    _roles->SetOnSelectionChanged([this](size_t selected)
                                  { _draft.audioRoles = selected ? AudioRoles::Communications : AudioRoles::All; });
    _restore = _panel->AddChild<DxUi::Checkbox>(L"Restore saved levels");
    _fields[5] = _restore;
    _restore->SetOnToggled(
        [this](bool value)
        {
            _draft.restoreLevels = value;
            Invalidate();
        });
    for (size_t i = 0; i < 2; ++i)
    {
        auto* slider = _savedLevels[i] = _panel->AddChild<DxUi::Slider>();
        _fields[i + 6] = slider;
        slider->SetStep(1);
        slider->SetLargeStep(5);
        slider->SetAccessibleName(captions[i + 6]);
        slider->SetOnChange(
            [this, i](DxUi::SliderChange change)
            {
                if (change.phase != DxUi::SliderChangePhase::Commit)
                    return;
                auto& value = i ? _draft.microphoneLevel : _draft.outputLevel;
                value = static_cast<uint32_t>(std::clamp(change.value, 0.0, 100.0));
                Invalidate();
            });
    }
}
void ProfileControls::SetConfiguration(const Configuration& configuration) noexcept
{
    _configuration = configuration;
    Invalidate();
}
ProfileControls::RetainedState ProfileControls::CaptureState() const noexcept
{
    RetainedState result;
    result.draft = _draft;
    result.error = _error;
    result.editingIndex = _editingIndex;
    result.mode = static_cast<uint32_t>(_mode);
    result.page = _page;
    const auto text = _name->GetText();
    std::copy_n(text.data(), (std::min)(text.size(), result.name.size() - 1), result.name.data());
    return result;
}
void ProfileControls::RestoreState(const RetainedState& state) noexcept
{
    _draft = state.draft;
    _error = state.error;
    _editingIndex = state.editingIndex;
    _mode = state.mode <= static_cast<uint32_t>(Mode::CameraSetup) ? static_cast<Mode>(state.mode) : Mode::Chooser;
    _page = state.page;
    _shown = true;
    _draftDirty = true;
    _optionsDirty = true;
    try
    {
        FillDraft();
        _name->SetText(state.name.data());
        _draftDirty = false;
    }
    catch (const std::bad_alloc&)
    {
        Error(L"Unable to restore the profile editor.");
    }
    Invalidate();
}
void ProfileControls::SetInventory(const Inventory* inventory, uint64_t deviceRevision) noexcept
{
    _inventory = inventory;
    if (_deviceRevision != deviceRevision)
    {
        _deviceRevision = deviceRevision;
        _optionsDirty = true;
        Invalidate();
    }
}
void ProfileControls::Invalidate() noexcept
{
    _dirty = true;
    if (_callbacks.invalidate)
        _callbacks.invalidate(_callbacks.context);
}
void ProfileControls::Error(std::wstring_view text) noexcept
{
    const size_t count = (std::min)(text.size(), _error.size() - 1);
    std::copy_n(text.data(), count, _error.data());
    _error[count] = 0;
    Invalidate();
}
void ProfileControls::Show() noexcept
{
    _shown = true;
    _mode = Mode::Chooser;
    _page = 0;
    _error[0] = 0;
    Invalidate();
}
void ProfileControls::Hide() noexcept
{
    _shown = false;
    _panel->SetVisible(false);
}
void ProfileControls::Back() noexcept
{
    if (_error[0])
    {
        _error[0] = 0;
        Invalidate();
        return;
    }
    if (_mode != Mode::Chooser)
    {
        _mode = Mode::Chooser;
        _page = 0;
        _error[0] = 0;
        Invalidate();
    }
    else if (_callbacks.close)
        _callbacks.close(_callbacks.context);
}
void ProfileControls::Activate(uint32_t index) noexcept
{
    if (index >= _configuration.count)
        return;
    if (_mode == Mode::Definitions)
        Edit(index);
    else if (_mode == Mode::Chooser && _callbacks.apply)
        _callbacks.apply(_callbacks.context, index);
}
void ProfileControls::Edit(size_t index) noexcept
{
    if (index >= _configuration.count && _configuration.count == MaximumProfiles)
    {
        Error(L"Four profiles maximum. Edit or delete one first.");
        return;
    }
    _editingIndex = index;
    _draft = index < _configuration.count ? _configuration.profiles[index] : Profile{};
    if (index >= _configuration.count)
    {
        for (uint32_t n = 1; n <= MaximumProfiles + 1; ++n)
        {
            char id[32]{};
            sprintf_s(id, "profile-%u", n);
            bool duplicate = false;
            for (uint32_t i = 0; i < _configuration.count; ++i)
                duplicate = duplicate || SameProfileId(_configuration.profiles[i].id.View(), id);
            if (!duplicate)
            {
                (void)_draft.id.Assign(id);
                break;
            }
        }
        (void)_draft.name.Assign("New profile");
        if (_inventory)
        {
            _draft.outputId = _inventory->state.output.id;
            _draft.microphoneId = _inventory->state.microphone.id;
            _draft.cameraId = _inventory->state.camera.sourceId;
            _draft.outputLevel = _inventory->state.output.level;
            _draft.microphoneLevel = _inventory->state.microphone.level;
        }
    }
    _mode = Mode::Editor;
    _page = 0;
    _draftDirty = _optionsDirty = true;
    _error[0] = 0;
    Invalidate();
}
void ProfileControls::Footer(size_t index) noexcept
{
    if (_error[0])
    {
        _error[0] = 0;
        Invalidate();
        return;
    }
    if (index == 0 && _page > 0)
        --_page;
    else if (index == 1 && _page + 1 < _pageCount)
        ++_page;
    else if (index == 2)
    {
        if (_mode == Mode::Chooser)
        {
            _mode = Mode::Definitions;
            _page = 0;
        }
        else if (_mode == Mode::Definitions)
            Edit(MaximumProfiles);
        else if (_mode == Mode::CameraSetup)
        {
            _mode = Mode::Chooser;
            _page = 0;
        }
        else
            Save(false);
    }
    Invalidate();
}
void ProfileControls::Save(bool remove) noexcept
{
    Configuration edited = _configuration;
    if (remove)
    {
        if (_editingIndex >= edited.count)
            return;
        for (size_t i = _editingIndex; i + 1 < edited.count; ++i)
            edited.profiles[i] = edited.profiles[i + 1];
        --edited.count;
        edited.profiles[edited.count] = {};
    }
    else
    {
        const auto name = _name->GetText();
        const int bytes = name.empty()
                              ? 0
                              : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(),
                                                    static_cast<int>(name.size()), _draft.name.bytes.data(),
                                                    static_cast<int>(_draft.name.bytes.size() - 1), nullptr, nullptr);
        if (bytes <= 0)
        {
            Error(L"Enter a name with at most 48 characters.");
            return;
        }
        _draft.name.length = static_cast<uint32_t>(bytes);
        _draft.name.bytes[static_cast<size_t>(bytes)] = 0;
        const size_t index = _editingIndex < edited.count ? _editingIndex : edited.count;
        if (index >= MaximumProfiles)
        {
            Error(L"Four profiles maximum.");
            return;
        }
        edited.profiles[index] = _draft;
        if (index == edited.count)
            ++edited.count;
    }
    std::array<char, MaximumSettingsBytes> json{};
    uint32_t written = 0;
    if (FAILED(SerializeConfiguration(edited, json.data(), json.size(), written)))
    {
        Error(L"Choose all three devices and a valid name (48 characters maximum). Profiles must fit the settings "
              L"limit.");
        return;
    }
    const HRESULT saved = _callbacks.save ? _callbacks.save(_callbacks.context, edited) : E_ACCESSDENIED;
    if (FAILED(saved))
    {
        Error(L"The profile could not be saved. Your previous definitions are unchanged.");
        return;
    }
    _configuration = edited;
    _mode = Mode::Chooser;
    _page = 0;
    _error[0] = 0;
    Invalidate();
}
void ProfileControls::FillDevices()
{
    for (size_t kind = 0; kind < 3; ++kind)
    {
        std::vector<DxUi::ComboBox::Item> items;
        items.reserve(MaximumOutputs + 1);
        const auto& selectedId = kind == 0 ? _draft.outputId : kind == 1 ? _draft.microphoneId : _draft.cameraId;
        std::optional<size_t> selected;
        _deviceCounts[kind] = 0;
        const uint32_t count = !_inventory ? 0
                               : kind == 0 ? _inventory->outputCount
                               : kind == 1 ? _inventory->inputCount
                                           : _inventory->cameraCount;
        for (uint32_t i = 0; i < count && i < (kind == 2 ? MaximumCameras : MaximumOutputs); ++i)
        {
            const auto& id = kind == 0   ? _inventory->outputs[i].id
                             : kind == 1 ? _inventory->inputs[i].id
                                         : _inventory->cameras[i].id;
            const auto& name = kind == 0   ? _inventory->outputs[i].name
                               : kind == 1 ? _inventory->inputs[i].name
                                           : _inventory->cameras[i].name;
            _deviceIds[kind][items.size()] = id;
            if (id == selectedId)
                selected = items.size();
            items.push_back({Wide(id.View()), name[0] ? std::wstring(name.data()) : Wide(id.View())});
        }
        if (!selected && !selectedId.View().empty())
        {
            selected = items.size();
            _deviceIds[kind][items.size()] = selectedId;
            items.push_back({Wide(selectedId.View()), L"Missing · " + Wide(selectedId.View())});
        }
        _deviceCounts[kind] = items.size();
        _devices[kind]->SetItems(std::move(items));
        _devices[kind]->SetSelectedIndex(selected);
        _devices[kind]->SetPlaceholder(L"Choose a device");
    }
    _optionsDirty = false;
}
void ProfileControls::FillDraft()
{
    _name->SetText(Wide(_draft.name.View()));
    _roles->SetSelectedIndex(_draft.audioRoles == AudioRoles::Communications ? 1 : 0);
    _restore->SetChecked(_draft.restoreLevels);
    _savedLevels[0]->SetValue(_draft.outputLevel);
    _savedLevels[1]->SetValue(_draft.microphoneLevel);
    _draftDirty = false;
}
DxUi::Control* ProfileControls::InitialFocus() const noexcept
{
    if (_mode == Mode::Editor)
        for (auto* field : _fields)
            if (field->IsVisible())
                return field;
    return _back;
}
void ProfileControls::Prepare(float width, float height)
{
    if (!_shown)
        return;
    if (!_dirty && width == _width && height == _height)
        return;
    _width = width;
    _height = height;
    _dirty = false;
    _panel->SetVisible(true);
    Bounds(_panel, 0, 0, width, height);
    const bool editing = _mode == Mode::Editor;
    const float bodyWidth = width - 16;
    Bounds(_back, 8, 8, 48, 48);
    Bounds(_title, 64, 8, bodyWidth - (editing && _editingIndex < _configuration.count ? 112 : 56), 48);
    _title->SetText(editing                      ? (width < 240 ? L"Edit" : L"Profile")
                    : _mode == Mode::Definitions ? (width < 320 ? L"Define" : L"Define profiles")
                                                 : L"Profiles");
    if (_inventory && _inventory->truncated)
        _title->SetText(width < 240   ? (editing && _editingIndex < _configuration.count ? L"!" : L"Limited")
                        : width < 480 ? L"Limited devices"
                        : editing     ? L"Profile · Limited devices"
                                      : L"Profiles · Limited devices");
    _title->SetAccessibleName(_inventory && _inventory->truncated
                                  ? L"Device list is limited. Defaults and saved profile bindings are shown first. "
                                    L"Some devices may be omitted."
                                  : std::wstring(_title->GetText()));
    _remove->SetVisible(editing && _editingIndex < _configuration.count);
    Bounds(_remove, width - 56, 8, 48, 48);
    for (auto* button : _profiles)
        button->SetVisible(false);
    _cameraSetup->SetVisible(false);
    for (size_t i = 0; i < _fields.size(); ++i)
    {
        _fields[i]->SetVisible(false);
        _captions[i]->SetVisible(false);
    }
    float available = height - 112;
    if (_mode == Mode::CameraSetup)
    {
        _pageCount = static_cast<uint32_t>(cameraSetupSteps.size());
        _page = (std::min)(_page, _pageCount - 1);
        if (_pageCount > 1)
            available = height - 132;
        wchar_t title[64]{};
        swprintf_s(title, width < 320 ? L"%u / %u" : L"Camera setup · %u / %u", _page + 1, _pageCount);
        _title->SetText(title);
        _title->SetAccessibleName(std::wstring(L"Camera setup, step ") + std::to_wstring(_page + 1) + L" of " +
                                  std::to_wstring(_pageCount));
    }
    else if (editing)
    {
        if (_draftDirty)
            FillDraft();
        if (_optionsDirty)
            FillDevices();
        const uint32_t columns = width >= 640 ? 2U : 1U;
        const uint32_t fields = _draft.restoreLevels ? 8U : 6U;
        auto paginate = [&](float space) noexcept
        {
            const uint32_t rows = (std::max)(1U, static_cast<uint32_t>(space / 80));
            const uint32_t perPage = columns * rows;
            _pageCount = (fields + perPage - 1) / perPage;
            _page = (std::min)(_page, _pageCount - 1);
            return perPage;
        };
        uint32_t perPage = paginate(available);
        if (_pageCount > 1)
        {
            available = height - 132;
            perPage = paginate(available);
        }
        const float fieldWidth = (bodyWidth - (columns - 1) * 16.0f) / columns;
        const float rowHeight = (std::min)(88.0f, available / (std::max)(1U, perPage / columns));
        for (uint32_t slot = 0; slot < perPage && _page * perPage + slot < fields; ++slot)
        {
            const uint32_t field = _page * perPage + slot;
            const float x = 8 + (slot % columns) * (fieldWidth + 16), y = 56 + (slot / columns) * rowHeight;
            _fields[field]->SetVisible(true);
            _captions[field]->SetVisible(true);
            Bounds(_captions[field], x, y, fieldWidth, 20);
            Bounds(_fields[field], x, y + 20, fieldWidth, 48);
        }
        wchar_t text[80]{};
        swprintf_s(text, L"Saved output volume · %u%%", _draft.outputLevel);
        _captions[6]->SetText(text);
        swprintf_s(text, L"Saved microphone gain · %u%%", _draft.microphoneLevel);
        _captions[7]->SetText(text);
    }
    else
    {
        const uint32_t entries = _configuration.count + (_mode == Mode::Chooser ? 1U : 0U);
        auto paginate = [&](float space) noexcept
        {
            const uint32_t perPage = (std::max)(1U, static_cast<uint32_t>(space / 56));
            _pageCount = (std::max)(1U, (entries + perPage - 1) / perPage);
            _page = (std::min)(_page, _pageCount - 1);
            return perPage;
        };
        uint32_t perPage = paginate(available);
        if (_pageCount > 1)
        {
            available = height - 132;
            perPage = paginate(available);
        }
        for (uint32_t slot = 0; slot < perPage && _page * perPage + slot < entries; ++slot)
        {
            const uint32_t index = _page * perPage + slot;
            if (index == _configuration.count)
            {
                _cameraSetup->SetVisible(true);
                Bounds(_cameraSetup, 8, 56 + slot * 56.0f, bodyWidth, 48);
                continue;
            }
            _profiles[index]->SetText((_mode == Mode::Definitions ? L"Edit · " : L"") +
                                      Wide(_configuration.profiles[index].name.View()));
            _profiles[index]->SetAccessibleName((_mode == Mode::Definitions ? L"Edit profile " : L"Apply profile ") +
                                                Wide(_configuration.profiles[index].name.View()));
            _profiles[index]->SetVisible(true);
            Bounds(_profiles[index], 8, 56 + slot * 56.0f, bodyWidth, 48);
        }
    }
    for (size_t i = 0; i < 3; ++i)
    {
        Bounds(_footer[i], 8 + static_cast<float>(i) * bodyWidth / 3, height - 56, bodyWidth / 3, 48);
        _footer[i]->SetVisible(true);
    }
    if (_pageCount == 1 && width >= 320)
    {
        _footer[0]->SetVisible(false);
        _footer[1]->SetVisible(false);
        Bounds(_footer[2], width - 168, height - 56, 160, 48);
    }
    _footer[0]->SetText(L"Prev");
    _footer[1]->SetText(L"Next");
    _footer[0]->SetAccessibleName(L"Previous");
    _footer[1]->SetAccessibleName(L"Next");
    _footer[0]->SetEnabled(!_error[0] && _page > 0);
    _footer[1]->SetEnabled(!_error[0] && _page + 1 < _pageCount);
    _footer[2]->SetText(_error[0] ? L"Edit" : editing ? L"Save" : _mode == Mode::Definitions ? L"New" : L"Define");
    _footer[2]->SetEnabled(_mode != Mode::Definitions || _configuration.count < MaximumProfiles);
    _footer[2]->SetAccessibleName(editing                      ? L"Save profile definition without changing devices"
                                  : _mode == Mode::Definitions ? L"Create a new profile"
                                                               : L"Define profiles");
    _message->SetVisible(_error[0] || (!editing && _configuration.count == 0));
    _message->SetText(_error[0] ? (available < 160 ? L"Cannot save.\nBack to edit." : _error.data())
                                : L"No profiles yet. Choose Define, then New.");
    _message->SetAccessibleName(_error[0] ? _error.data() : L"No profiles yet. Choose Define, then New.");
    _message->SetFontRole(DxUi::FontRole::Body);
    if (_mode == Mode::Chooser && !_error[0])
        _message->SetVisible(false);
    if (_mode == Mode::CameraSetup)
    {
        const auto& step = cameraSetupSteps[_page];
        _message->SetVisible(true);
        _message->SetText(std::wstring(width < 320 || available < 160 ? step.shortText : step.detail));
        _message->SetAccessibleName(std::wstring(step.detail));
        _message->SetFontRole(width >= 800 && height >= 360   ? DxUi::FontRole::Title
                              : width >= 320 && height >= 300 ? DxUi::FontRole::BodyLarge
                                                              : DxUi::FontRole::Body);
        _footer[2]->SetText(L"Done");
        _footer[2]->SetAccessibleName(L"Return to AV profiles without changing devices");
        if (width >= 320)
        {
            const float buttonWidth = (std::min)(160.0f, (bodyWidth - 16) / 3);
            const float start = (width - (buttonWidth * 3 + 16)) / 2;
            for (size_t i = 0; i < 3; ++i)
                Bounds(_footer[i], start + static_cast<float>(i) * (buttonWidth + 8), height - 56, buttonWidth, 48);
        }
    }
    // Error is a dedicated temporary body state. Navigation stays visible and no live control is covered.
    if (_error[0])
    {
        for (auto* field : _fields)
            field->SetVisible(false);
        for (auto* caption : _captions)
            caption->SetVisible(false);
        for (auto* button : _profiles)
            button->SetVisible(false);
    }
    Bounds(_message, 8, 56, bodyWidth, available);
    if (_mode == Mode::CameraSetup && width >= 800)
        Bounds(_message, (width - 720) / 2, 72, 720, height - 144);
    _indicator->SetPageCount(_pageCount);
    _indicator->SetSelectedIndex(_page);
    _indicator->SetVisible(_pageCount > 1 && !_error[0]);
    if (_pageCount > 1)
        Bounds(_indicator, 8, height - 76, bodyWidth, DxUi::PageIndicator::kStripHeightDip);
}
} // namespace AVControl
