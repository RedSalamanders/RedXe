#include "AVControlView.h"
#include "AccessibilitySite.h"
#include "AccessibilityValidation.h"
#include "DxUiTextTransport.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace AVControl
{
namespace
{
D2D1_RECT_F Bounds(Rect r) noexcept
{
    return D2D1::RectF(r.x, r.y, r.x + r.width, r.y + r.height);
}
constexpr std::array names{L"Output", L"Microphone", L"Camera"};
constexpr std::array shortNames{L"Out", L"Mic", L"Cam"};
// Segoe Fluent Icons; Unicode stand-ins when the icon font is missing.
constexpr wchar_t FluentToggleGlyph(size_t index, bool off) noexcept
{
    if (index == 0)
        return off ? L'\uE74F' : L'\uE767';
    if (index == 1)
        return off ? L'\uF781' : L'\uE720';
    return off ? L'\uF403' : L'\uE714';
}
const wchar_t* FallbackToggleGlyph(size_t index, bool off) noexcept
{
    static constexpr const wchar_t* on[]{L"\U0001F50A", L"\U0001F3A4", L"\U0001F3A5"};
    static constexpr const wchar_t* muted[]{L"\U0001F507", L"\U0001F3A4", L"\U0001F4F7"};
    return off ? muted[index] : on[index];
}
DxUi::FontRole IconFontRole(bool fluent, float sizeDip) noexcept
{
    if (!fluent)
        return sizeDip >= 24.0f ? DxUi::FontRole::BodyLarge : DxUi::FontRole::Body;
    if (sizeDip >= 52.0f)
        return DxUi::FontRole::HeroIcon;
    if (sizeDip >= 28.0f)
        return DxUi::FontRole::IconLarge;
    return DxUi::FontRole::Icon;
}
const wchar_t* AvailabilityText(Availability value) noexcept
{
    switch (value)
    {
    case Availability::Unknown:
        return L"Loading";
    case Availability::Ready:
        return L"Ready";
    case Availability::Missing:
        return L"Missing";
    case Availability::AccessDenied:
        return L"Denied";
    case Availability::Busy:
        return L"Busy";
    case Availability::Unsupported:
        return L"N/A";
    default:
        return L"Error";
    }
}
bool CanRetryCamera(const CameraState& state) noexcept
{
    return !state.sourceId.View().empty() && state.revision && !state.enabled &&
           (state.availability == Availability::AccessDenied || state.availability == Availability::Busy ||
            state.availability == Availability::Failed);
}
class ViewSurface final : public DxUi::Panel
{
  public:
    void Paint(DxUi::ControlHost& host) const override
    {
        // EmbeddedHost deliberately preserves transparency. AV owns an opaque theme surface so a light or
        // contrast foreground never inherits the dashboard's dark background through empty panel pixels.
        if (auto* context = host.GetDeviceContext())
            if (auto* brush = host.GetSolidBrush(host.GetTheme().windowBackground))
                context->FillRectangle(GetBounds(), brush);
        DxUi::Panel::Paint(host);
    }
};
// Preserve the card presentation and command callback while exposing the retained Toggle pattern. Only a
// confirmed backend snapshot changes checked state; pointer, keyboard and UIA activation never flip it locally.
class ConfirmedToggle final : public DxUi::Toggle
{
  public:
    void Paint(DxUi::ControlHost& host) const override
    {
        DxUi::Button::Paint(host);
    }
    bool OnMouseUp(DxUi::ControlHost& host, D2D1_POINT_2F point, bool rightButton, UINT modifiers) override
    {
        return DxUi::Button::OnMouseUp(host, point, rightButton, modifiers);
    }
    bool OnKeyDown(DxUi::ControlHost& host, UINT key, UINT modifiers) override
    {
        return DxUi::Button::OnKeyDown(host, key, modifiers);
    }
    bool OnMnemonic(DxUi::ControlHost& host) override
    {
        return DxUi::Button::OnMnemonic(host);
    }
};
} // namespace

HRESULT LiveView::Attach(std::shared_ptr<DxUi::GraphicsDevice> graphics, ViewCallbacks callbacks) noexcept
{
    _callbacks = callbacks;
    const HRESULT hr = _view.Attach(std::move(graphics), {this, [](void* context) noexcept
                                                          {
                                                              auto& self = *static_cast<LiveView*>(context);
                                                              if (self._callbacks.requestFrame)
                                                                  self._callbacks.requestFrame(self._callbacks.context);
                                                          }});
    if (FAILED(hr))
        return hr;
    try
    {
        ApplyTheme();
        Build();
        _modelDirty = true;
        return S_OK;
    }
    catch (...)
    {
        Detach();
        return E_OUTOFMEMORY;
    }
}

void LiveView::Build()
{
    auto tree = std::make_unique<ViewSurface>();
    auto* root = _livePanel = tree->AddChild<DxUi::Panel>();
    _heading = root->AddChild<DxUi::Label>(L"AV Control");
    _profile = root->AddChild<DxUi::Button>(L"Profiles");
    _profile->SetAccessibleName(L"Choose an AV profile");
    _profile->SetAccessibleAutomationId(L"av.profiles");
    _profile->SetOnClick([this] { Emit({ViewCommandKind::OpenProfiles}); });
    _profileCaption = root->AddChild<DxUi::Label>(L"Profile");
    _profileCaption->SetFontRole(DxUi::FontRole::Small);
    _profileCaption->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    for (size_t i = 0; i < _toggles.size(); ++i)
    {
        auto* button = _toggles[i] = root->AddChild<ConfirmedToggle>();
        button->SetAccessibleAutomationId(std::wstring(L"av.toggle.") + shortNames[i]);
        button->SetOnClick([this, i] { Toggle(i); });
        _toggleIcons[i] = root->AddChild<DxUi::Label>();
        _toggleIcons[i]->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        _toggleNames[i] = root->AddChild<DxUi::Label>();
        _toggleNames[i]->SetAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        _toggleNames[i]->SetMultiline(false);
    }
    for (size_t i = 0; i < _sliders.size(); ++i)
    {
        _levelCards[i] = root->AddChild<DxUi::CardPanel>();
        _levelIcons[i] = root->AddChild<DxUi::Label>();
        _levelIcons[i]->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        _levelValues[i] = root->AddChild<DxUi::Label>();
        _levelValues[i]->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        _levelValues[i]->SetMultiline(false);
        _sliders[i] = root->AddChild<DxUi::Slider>();
        _sliders[i]->SetMinimum(0);
        _sliders[i]->SetMaximum(100);
        _sliders[i]->SetStep(1);
        _sliders[i]->SetLargeStep(5);
        _sliders[i]->SetAccessibleName(i ? L"Microphone gain, Windows input level" : L"Global output volume");
        _sliders[i]->SetAccessibleHelpText(i ? L"Controls the selected Windows capture endpoint, preserving mute."
                                             : L"Controls applications using this output endpoint, preserving mute.");
        _sliders[i]->SetAccessibleAutomationId(i ? L"av.level.microphone" : L"av.level.output");
        _sliders[i]->SetOnChange([this, i](DxUi::SliderChange change) { SliderChanged(i, change); });
    }
    _view.Controls().SetRoot(std::move(tree));
    _view.Controls().SetOnTabBoundary(
        [this](bool)
        {
            if (_profilesVisible)
                return false;
            _tabBoundary = true;
            return true;
        });
}

void LiveView::SetAppearance(const RedXeAppearance& appearance) noexcept
{
    if (_appearance == appearance)
        return;
    _appearance = appearance;
    _themeDirty = true;
    _modelDirty = true;
    _view.MarkDirty();
}
void LiveView::ApplyTheme()
{
    auto theme = DxUi::MakeDefaultThemePalette((_appearance.flags & RedXeAppearanceDark) != 0);
    theme.reducedMotion = true; // Live acknowledgements never add animation-only frames.
    theme.highContrast = (_appearance.flags & RedXeAppearanceHighContrast) != 0;
    if (theme.highContrast)
    {
        const auto color = [](uint32_t argb) noexcept
        { return D2D1::ColorF(((argb >> 16) & 255) / 255.0f, ((argb >> 8) & 255) / 255.0f, (argb & 255) / 255.0f); };
        const auto window = color(_appearance.window), text = color(_appearance.windowText);
        const auto selected = color(_appearance.highlight), selectedText = color(_appearance.highlightText);
        const auto button = color(_appearance.button), buttonText = color(_appearance.buttonText);
        theme.windowBackground = theme.surfaceBackground = theme.cardBackground = theme.overlayBackground = window;
        theme.headerBackground = theme.headerHovered = theme.headerPressed = window;
        theme.text = theme.subduedText = text;
        theme.disabledText = color(_appearance.disabledText);
        theme.border = theme.borderDefault = theme.borderStrong = theme.gridLine = theme.overlayBorder = text;
        theme.buttonFill = theme.buttonHotFill = theme.buttonPressedFill = button;
        theme.buttonBorder = buttonText;
        theme.inputFill = window;
        theme.inputBorder = text;
        theme.selectionFill = theme.selectionInactiveFill = theme.accent = theme.accentHover = theme.accentPressed =
            selected;
        theme.selectionText = theme.toggleKnobCheckedFill = selectedText;
        theme.toggleKnobFill = text;
        theme.focusStroke = theme.focusStrokeOuter = text;
        theme.focusStrokeInner = window;
        theme.hoverFill = theme.pressedFill = button;
        theme.scrollbarTrack = window;
        theme.scrollbarThumb = theme.scrollbarThumbHot = text;
        theme.tooltipBackground = window;
        theme.tooltipText = text;
        theme.infoFill = theme.warningFill = theme.errorFill = window;
        theme.infoText = theme.warningText = theme.errorText = text;
    }
    _view.Controls().SetTheme(theme);
    _themeDirty = false;
}

void LiveView::Detach() noexcept
{
    DisconnectAccessibility();
    _view.CancelTextInput();
    if (_profilesVisible && _profileControls)
        _retainedProfile = _profileControls->CaptureState();
    Cancel();
    _ready = false;
    _profileControls.reset();
    _view.Detach();
    _width = _height = 0;
}
void LiveView::SetVisible(bool visible) noexcept
{
    if (!visible)
    {
        DisconnectAccessibility();
        Cancel();
    }
    _visible = visible;
    _view.SetVisible(visible);
}
void LiveView::SetState(const ConfirmedState& state, std::wstring_view profileName, uint32_t pendingMask) noexcept
{
    if (_state.output.id != state.output.id || _state.output.generation != state.output.generation ||
        _state.output.levelRevision != state.output.levelRevision || _state.microphone.id != state.microphone.id ||
        _state.microphone.generation != state.microphone.generation ||
        _state.microphone.levelRevision != state.microphone.levelRevision)
        Cancel();
    _state = state;
    const size_t count = (std::min)(profileName.size(), _profileName.size() - 1);
    std::copy_n(profileName.data(), count, _profileName.data());
    _profileName[count] = 0;
    _pendingMask = pendingMask;
    _modelDirty = true;
    _view.MarkDirty();
}

void LiveView::Arrange()
{
    _livePanel->SetBounds(D2D1::RectF(0, 0, _width * 96.0f / _dpi, _height * 96.0f / _dpi));
    const bool usable = _layout.density != Density::Unusable;
    const bool minimal = _layout.density == Density::Minimal;
    const bool fluent = _view.Controls().HasFluentIconFont();
    _heading->SetBounds(Bounds(_layout.header));
    _heading->SetVisible(minimal || !usable);
    _profile->SetBounds(Bounds(_layout.profileSelector));
    _profile->SetVisible(usable);
    _profileCaption->SetBounds(Bounds(_layout.profileSelector));
    _profileCaption->SetVisible(usable && minimal);
    for (size_t i = 0; i < 3; ++i)
    {
        const Rect r = _layout.toggles[i];
        _toggles[i]->SetBounds(Bounds(r));
        _toggles[i]->SetVisible(usable);
        _toggleIcons[i]->SetVisible(usable);
        const bool named = usable && _layout.showDeviceNames;
        _toggleNames[i]->SetVisible(named);
        const float icon = named ? (std::clamp)(r.height - 16.0f, 32.0f, 64.0f)
                                 : (std::clamp)((std::min)(r.width, r.height) - 12.0f, 32.0f, 64.0f);
        _toggleIcons[i]->SetFontRole(IconFontRole(fluent, icon));
        if (named)
        {
            const float iconX = r.x + 10;
            const float iconY = r.y + (r.height - icon) * 0.5f;
            _toggleIcons[i]->SetBounds(D2D1::RectF(iconX, iconY, iconX + icon, iconY + icon));
            _toggleNames[i]->SetBounds(D2D1::RectF(iconX + icon + 8, r.y + 4, r.x + r.width - 10, r.y + r.height - 4));
            _toggleNames[i]->SetAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            _toggleNames[i]->SetFontRole(minimal ? DxUi::FontRole::Body : DxUi::FontRole::BodyLarge);
        }
        else
        {
            const float iconX = r.x + (r.width - icon) * 0.5f;
            const float iconY = r.y + (r.height - icon) * 0.5f;
            _toggleIcons[i]->SetBounds(D2D1::RectF(iconX, iconY, iconX + icon, iconY + icon));
        }
    }
    for (size_t i = 0; i < 2; ++i)
    {
        _sliders[i]->SetVisible(usable);
        _sliders[i]->SetBounds(Bounds(_layout.sliders[i]));
        const Rect panel = _layout.levelPanels[i];
        const Rect slider = _layout.sliders[i];
        _levelCards[i]->SetBounds(Bounds(panel));
        _levelCards[i]->SetVisible(false);
        _levelIcons[i]->SetVisible(usable);
        _levelValues[i]->SetVisible(usable);
        const float leadingRight = slider.x;
        const float leading = (std::max)(0.0f, leadingRight - panel.x);
        const float icon = (std::clamp)(leading * 0.45f, 28.0f, 40.0f);
        _levelIcons[i]->SetFontRole(IconFontRole(fluent, icon));
        const float rowTop = slider.y;
        const float rowBottom = slider.y + slider.height;
        _levelIcons[i]->SetBounds(D2D1::RectF(panel.x, rowTop, panel.x + icon, rowBottom));
        _levelValues[i]->SetBounds(D2D1::RectF(panel.x + icon, rowTop, leadingRight, rowBottom));
        _levelValues[i]->SetFontRole(minimal ? DxUi::FontRole::Small : DxUi::FontRole::BodyLarge);
        _levelValues[i]->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void LiveView::Refresh()
{
    const bool minimal = _layout.density == Density::Minimal;
    const bool fluent = _view.Controls().HasFluentIconFont();
    _heading->SetText(_layout.density == Density::Unusable
                          ? L"Increase tile size"
                          : (minimal ? (_profileName[0] ? _profileName.data() : L"Custom") : L"Live controls"));
    _profile->SetText(minimal           ? L""
                      : _profileName[0] ? std::wstring(_profileName.data()) + L" · Profiles"
                                        : L"Custom · Profiles");
    _profile->SetAccessibleName(std::wstring(L"Choose an AV profile. Current: ") +
                                (_profileName[0] ? _profileName.data() : L"Custom"));
    for (size_t i = 0; i < 3; ++i)
    {
        const auto availability = i == 2 ? _state.camera.availability : Audio(i).availability;
        const bool enabled = availability == Availability::Ready;
        const bool retry = i == 2 && CanRetryCamera(_state.camera);
        const bool off = i == 2 ? !_state.camera.enabled : Audio(i).muted;
        const wchar_t* stateText = !enabled ? AvailabilityText(availability)
                                   : off    ? (i == 2 ? L"Off" : L"Muted")
                                            : L"On";
        const bool pending = (_pendingMask & (1U << i)) != 0;
        const wchar_t* caption = pending ? L"Wait…" : enabled ? DeviceName(i) : stateText;
        _toggles[i]->SetEnabled(enabled || retry);
        _toggles[i]->SetChecked(enabled && off);
        _toggles[i]->SetPrimary(enabled && off);
        const auto& theme = _view.Controls().GetTheme();
        const auto labelColor = !enabled && !retry ? theme.disabledText
                                : enabled && off   ? theme.selectionText
                                                   : theme.text;
        _toggleIcons[i]->SetTextColor(labelColor);
        _toggleNames[i]->SetTextColor(labelColor);
        const auto iconBounds = _toggleIcons[i]->GetBounds();
        _toggleIcons[i]->SetFontRole(IconFontRole(fluent, iconBounds.bottom - iconBounds.top));
        _toggleIcons[i]->SetText(fluent ? std::wstring(1, FluentToggleGlyph(i, off))
                                        : std::wstring(FallbackToggleGlyph(i, off)));
        _toggleNames[i]->SetText(caption);
        std::wstring accessible = names[i];
        if (enabled)
        {
            accessible += L", ";
            accessible += DeviceName(i);
        }
        accessible += L", ";
        accessible += stateText;
        accessible += enabled ? (off ? L". Turn on" : L". Turn off") : retry ? L". Retry camera" : L". Unavailable";
        _toggles[i]->SetAccessibleName(std::move(accessible));
        _toggles[i]->SetAccessibleHelpText(
            pending ? L"Change pending. Safety off remains available."
            : retry ? L"Resolve camera access or close the other capture app, then activate to retry."
                    : L"");
    }
    for (size_t i = 0; i < 2; ++i)
    {
        const auto& endpoint = Audio(i);
        const bool enabled = endpoint.availability == Availability::Ready;
        const uint32_t value = _gestures[i].Active() ? _gestures[i].Draft() : endpoint.level;
        wchar_t caption[16]{};
        if (!enabled)
            wcscpy_s(caption, L"—");
        else
            swprintf_s(caption, L"%u%s", value, (_pendingMask & (1U << (i + 3))) ? L"…" : L"");
        const auto iconBounds = _levelIcons[i]->GetBounds();
        _levelIcons[i]->SetFontRole(IconFontRole(fluent, iconBounds.bottom - iconBounds.top));
        _levelIcons[i]->SetText(fluent ? std::wstring(1, FluentToggleGlyph(i, false))
                                       : std::wstring(FallbackToggleGlyph(i, false)));
        _levelValues[i]->SetText(caption);
        _sliders[i]->SetEnabled(enabled);
        _sliders[i]->SetValue(value);
    }
}

HRESULT LiveView::Prepare(uint32_t width, uint32_t height, float dpi) noexcept
{
    if (!std::isfinite(dpi) || dpi <= 0)
        return E_INVALIDARG;
    if (!_visible || !width || !height)
        return S_FALSE;
    try
    {
        if (_themeDirty)
            ApplyTheme();
        if (width != _width || height != _height || dpi != _dpi)
        {
            Cancel();
            ++_layoutRevision;
            _width = width;
            _height = height;
            _dpi = dpi;
            _layout = LayoutLive(width * 96.0f / dpi, height * 96.0f / dpi);
            Arrange();
            _modelDirty = true;
        }
        _livePanel->SetVisible(!_profilesVisible);
        if (_profilesVisible)
        {
            if (!_profileControls)
            {
                _profileControls = std::make_unique<ProfileControls>(
                    *static_cast<DxUi::Panel*>(_view.Controls().GetRoot()),
                    ProfileCallbacks{
                        this,
                        [](void* context, uint32_t index) noexcept
                        {
                            auto& self = *static_cast<LiveView*>(context);
                            self.CloseProfiles();
                            self.Emit({ViewCommandKind::ApplyProfile, {}, {}, 0, 0, index});
                        },
                        [](void* context, const Configuration& configuration) noexcept
                        {
                            auto& self = *static_cast<LiveView*>(context);
                            return self._callbacks.saveProfiles
                                       ? self._callbacks.saveProfiles(self._callbacks.context, configuration)
                                       : E_ACCESSDENIED;
                        },
                        [](void* context) noexcept
                        {
                            auto& self = *static_cast<LiveView*>(context);
                            self.CloseProfiles();
                            self.Emit({ViewCommandKind::CloseProfiles});
                        },
                        [](void* context) noexcept { static_cast<LiveView*>(context)->_view.MarkDirty(); }});
                _profileControls->SetConfiguration(_configuration);
                _profileControls->SetInventory(_inventory, _deviceRevision);
                _profileControls->Show();
                if (_retainedProfile)
                {
                    _profileControls->RestoreState(*_retainedProfile);
                    _retainedProfile.reset();
                }
            }
            _profileControls->Prepare(width * 96.0f / dpi, height * 96.0f / dpi);
            if (_profileFocusPending || !_view.Controls().GetFocusControl())
            {
                _view.Controls().SetFocusControl(_profileControls->InitialFocus());
                _profileFocusPending = false;
            }
        }
        else
        {
            if (_profileControls)
                _profileControls->Hide();
            if (_modelDirty)
            {
                Refresh();
                _modelDirty = false;
            }
        }
        const HRESULT hr = _view.Prepare(width, height, dpi);
        _ready = SUCCEEDED(hr) && width && height;
        return hr;
    }
    catch (...)
    {
        _ready = false;
        return E_OUTOFMEMORY;
    }
}

HRESULT LiveView::Composite(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport) noexcept
{
    return _ready ? _view.Composite(context, viewport) : S_FALSE;
}
bool LiveView::Pointer(const DxUi::PointerEvent& event) noexcept
{
    return _ready && _view.DispatchPointer(event);
}
bool LiveView::Key(UINT key, bool down, UINT modifiers) noexcept
{
    _tabBoundary = false;
    if (_ready && down && key == VK_ESCAPE)
    {
        DxUi::EmbeddedTextInputSnapshot text;
        if (_view.ReadTextInput(text) == S_OK && text.state.compositionStartIndex)
            return _view.DispatchKey(key, down, modifiers);
    }
    if (_ready && down && key == VK_ESCAPE && _profilesVisible && _profileControls)
    {
        _profileControls->Back();
        return true;
    }
    return _ready && _view.DispatchKey(key, down, modifiers);
}
bool LiveView::Character(wchar_t character, UINT modifiers) noexcept
{
    return _ready && _view.DispatchCharacter(character, modifiers);
}
HRESULT LiveView::ReadText(RedXeTextState& state) noexcept
{
    state = {};
    if (!_ready || !_visible)
        return S_FALSE;
    DxUi::EmbeddedTextInputSnapshot snapshot;
    const HRESULT read = _view.ReadTextInput(snapshot);
    if (read != S_OK)
        return read;
    return RedXeTextTransport::EncodeTextState(snapshot.revision, snapshot.focusId, snapshot.state,
                                               snapshot.caretBoundsDip, snapshot.viewportBoundsDip, _dpi, state);
}
HRESULT LiveView::ApplyText(uint32_t action, const RedXeTextState& state) noexcept
{
    if (action > RedXeTextCancel || !IsValidRedXeTextState(state))
        return E_INVALIDARG;
    if (!_ready || !_visible)
        return S_FALSE;
    DxUi::EmbeddedTextInputSnapshot current;
    const HRESULT read = _view.ReadTextInput(current);
    if (read != S_OK)
        return read;
    if (current.focusId != state.focusId || current.revision != state.revision)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    DxUi::NativeTextInputState change;
    const HRESULT decoded = RedXeTextTransport::DecodeTextState(state, change);
    if (decoded != S_OK)
        return decoded;
    return _view.ApplyTextInput(state.revision, change, static_cast<DxUi::EmbeddedTextInputAction>(action));
}
HRESULT LiveView::CancelText(uint64_t focusId) noexcept
{
    if (!focusId)
        return E_INVALIDARG;
    if (!_ready || !_visible)
        return S_FALSE;
    DxUi::EmbeddedTextInputSnapshot current;
    const HRESULT read = _view.ReadTextInput(current);
    if (read != S_OK)
        return read;
    if (current.focusId != focusId)
        return S_FALSE;
    return _view.ApplyTextInput(current.revision, {}, DxUi::EmbeddedTextInputAction::Cancel);
}
HRESULT LiveView::HitText(uint64_t revision, float x, float y, uint32_t& index) noexcept
{
    index = 0;
    if (!_ready || !_visible)
        return S_FALSE;
    size_t acp = 0;
    const HRESULT hr = _view.HitTestTextInput(revision, {x * 96 / _dpi, y * 96 / _dpi}, acp);
    if (hr == S_OK)
    {
        if (acp > kRedXeMaximumTextUnits)
            return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        index = static_cast<uint32_t>(acp);
    }
    return hr;
}
HRESULT LiveView::TextRange(uint64_t revision, uint32_t start, uint32_t end, RedXeTextRectangle& bounds,
                            BOOL& clipped) noexcept
{
    bounds = {};
    clipped = TRUE;
    if (!_ready || !_visible)
        return S_FALSE;
    D2D1_RECT_F dip{};
    bool wasClipped = true;
    const HRESULT hr = _view.GetTextInputRangeBounds(revision, start, end, dip, wasClipped);
    if (hr != S_OK)
        return hr;
    const float scale = _dpi / 96;
    bounds = {dip.left * scale, dip.top * scale, dip.right * scale, dip.bottom * scale};
    clipped = wasClipped ? TRUE : FALSE;
    return S_OK;
}
HRESULT LiveView::ConnectAccessibility(const RedXeAccessibilityPlacement& placement, IRedXeAccessibilitySite* site,
                                       IRawElementProviderFragmentRoot** result) noexcept
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!site || !IsValidRedXeAccessibilityPlacement(placement))
        return E_INVALIDARG;
    if (!_ready || !_visible)
        return S_FALSE;
    DisconnectAccessibility();
    try
    {
        auto adapter = std::make_shared<AccessibilitySite>(site);
        const DxUi::EmbeddedAccessibilityPlacement geometry{
            {placement.left, placement.top, placement.width, placement.height}, placement.keyboardFocused != FALSE};
        const HRESULT attached = _view.AttachAccessibility(std::move(adapter), placement.attachmentId, geometry);
        if (attached != S_OK)
            return attached;
        _accessibilityIdentity = placement.attachmentId;
        const HRESULT provider = _view.GetAccessibilityProvider(result);
        if (provider != S_OK)
            DisconnectAccessibility();
        return provider;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}
HRESULT LiveView::UpdateAccessibility(const RedXeAccessibilityPlacement& placement) noexcept
{
    if (!IsValidRedXeAccessibilityPlacement(placement))
        return E_INVALIDARG;
    if (!_ready || !_visible || placement.attachmentId != _accessibilityIdentity)
        return S_FALSE;
    // A UIA command may dirty layout before its posted host focus/action is drained. Keep this
    // connection alive; the next successful preparation publishes its new geometry and state.
    if (_view.NeedsPreparation())
        return S_OK;
    wil::com_ptr_nothrow<IRawElementProviderFragmentRoot> provider;
    const HRESULT current = _view.GetAccessibilityProvider(provider.put());
    if (current != S_OK)
        return current;
    const DxUi::EmbeddedAccessibilityPlacement geometry{
        {placement.left, placement.top, placement.width, placement.height}, placement.keyboardFocused != FALSE};
    const HRESULT updated = _view.UpdateAccessibility(geometry);
    return updated == S_FALSE ? S_OK : updated;
}
void LiveView::DisconnectAccessibility() noexcept
{
    _accessibilityIdentity = 0;
    _view.DisconnectAccessibility();
}
void LiveView::SetProfiles(const Configuration& configuration, const Inventory* inventory,
                           uint64_t deviceRevision) noexcept
{
    _configuration = configuration;
    _inventory = inventory;
    _deviceRevision = deviceRevision;
    if (_profileControls)
    {
        _profileControls->SetConfiguration(configuration);
        _profileControls->SetInventory(inventory, deviceRevision);
    }
}
void LiveView::ShowProfiles() noexcept
{
    _profilesVisible = true;
    _profileFocusPending = true;
    if (_profileControls)
        _profileControls->Show();
    _view.MarkDirty();
}
void LiveView::CloseProfiles() noexcept
{
    _view.CancelTextInput();
    _profilesVisible = false;
    _retainedProfile.reset();
    _modelDirty = true;
    _view.MarkDirty();
}
void LiveView::Cancel() noexcept
{
    _view.DispatchPointer({DxUi::PointerAction::Cancel});
    for (auto& gesture : _gestures)
        gesture.Cancel();
}
void LiveView::Emit(ViewCommand command) noexcept
{
    if (_callbacks.command)
        _callbacks.command(_callbacks.context, command);
}
void LiveView::Toggle(size_t index) noexcept
{
    if (index == 2)
    {
        if (_state.camera.availability != Availability::Ready && !CanRetryCamera(_state.camera))
            return;
        Emit({ViewCommandKind::SetCameraEnabled, DeviceKind::Camera, _state.camera.sourceId, 0, _state.camera.revision,
              _state.camera.enabled ? 0U : 1U});
    }
    else
    {
        const auto& endpoint = Audio(index);
        if (endpoint.availability != Availability::Ready)
            return;
        Emit({ViewCommandKind::SetMuted, index ? DeviceKind::Microphone : DeviceKind::Output, endpoint.id,
              endpoint.generation, endpoint.muteRevision, endpoint.muted ? 0U : 1U});
    }
}
void LiveView::SliderChanged(size_t index, DxUi::SliderChange change) noexcept
{
    auto& gesture = _gestures[index];
    if (change.phase == DxUi::SliderChangePhase::Cancel)
    {
        gesture.Cancel();
        _modelDirty = true;
        return;
    }
    if (!gesture.Active() && !gesture.Begin(Audio(index), _layoutRevision))
        return;
    const auto value = static_cast<uint32_t>(std::clamp(std::round(change.value), 0.0, 100.0));
    if (!gesture.Preview(value))
        return;
    _modelDirty = true;
    if (change.phase == DxUi::SliderChangePhase::Commit)
    {
        uint32_t committed = 0;
        if (gesture.Commit(Audio(index), _layoutRevision, committed))
        {
            const auto& endpoint = Audio(index);
            Emit({ViewCommandKind::SetLevel, index ? DeviceKind::Microphone : DeviceKind::Output, endpoint.id,
                  endpoint.generation, endpoint.levelRevision, committed});
        }
    }
}

const wchar_t* LiveView::DeviceName(size_t index) const noexcept
{
    if (index == 2)
    {
        if (_inventory)
        {
            for (uint32_t i = 0; i < _inventory->cameraCount; ++i)
                if (_inventory->cameras[i].id == _state.camera.sourceId && _inventory->cameras[i].name[0])
                    return _inventory->cameras[i].name.data();
        }
        return L"RedXe Camera";
    }
    return Audio(index).name[0] ? Audio(index).name.data() : L"Selected audio device";
}
} // namespace AVControl
