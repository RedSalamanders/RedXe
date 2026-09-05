#include "AVControlView.h"
#include "DxUiTextTransport.h"
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <wil/result.h>
#include <wincodec.h>

namespace
{
uint32_t nativeChecks = 0;
void Check(bool value, const char* message)
{
    ++nativeChecks;
    if (!value)
        throw std::runtime_error(message);
}
void Hr(HRESULT hr, const char* message)
{
    Check(SUCCEEDED(hr), message);
}
struct Fixture final
{
    struct AccentCheck final
    {
        AVControl::Rect bounds;
        uint32_t fill;
        uint32_t text;
    };
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    wil::com_ptr_nothrow<ID3D11Texture2D> target;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> rtv;
    uint32_t width = 0, height = 0;
    Fixture()
    {
        Hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                             D3D11_SDK_VERSION, device.put(), nullptr, context.put()),
           "AV supplied WARP device");
    }
    void Resize(uint32_t w, uint32_t h)
    {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        rtv.reset();
        target.reset();
        width = w;
        height = h;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        Hr(device->CreateTexture2D(&desc, nullptr, target.put()), "AV render target");
        Hr(device->CreateRenderTargetView(target.get(), nullptr, rtv.put()), "AV render target view");
    }
    void Draw(AVControl::LiveView& view)
    {
        auto* renderTarget = rtv.get();
        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        constexpr float background[]{0.035f, 0.04f, 0.055f, 1};
        context->ClearRenderTargetView(renderTarget, background);
        Check(view.Composite(context.get(), {0, 0, float(width), float(height), 0, 1}) == S_OK,
              "AV cached view composites");
    }
    void Save(const std::filesystem::path& path, std::optional<uint32_t> expectedBackground = std::nullopt,
              std::optional<AccentCheck> expectedAccent = std::nullopt)
    {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        D3D11_TEXTURE2D_DESC desc{};
        target->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        wil::com_ptr_nothrow<ID3D11Texture2D> staging;
        Hr(device->CreateTexture2D(&desc, nullptr, staging.put()), "AV snapshot staging");
        context->CopyResource(staging.get(), target.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Hr(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped), "AV snapshot map");
        const auto unmap = wil::scope_exit([&] { context->Unmap(staging.get(), 0); });
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y)
            memcpy(pixels.data() + size_t(y) * width * 4,
                   static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(width) * 4);
        if (expectedBackground)
        {
            uint32_t corner = 0;
            memcpy(&corner, pixels.data(), sizeof(corner));
            Check(corner == *expectedBackground,
                  "AV supplies an opaque contrast surface instead of exposing the dark dashboard");
        }
        if (expectedAccent)
        {
            size_t fillPixels = 0, textPixels = 0, sampled = 0;
            const auto& expected = *expectedAccent;
            // Sample inside the card, excluding its border and neighboring background.
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x)
                    if (x > expected.bounds.x + 8 && x < expected.bounds.x + expected.bounds.width - 8 &&
                        y > expected.bounds.y + 8 && y < expected.bounds.y + expected.bounds.height - 8)
                    {
                        uint32_t pixel = 0;
                        memcpy(&pixel, pixels.data() + (size_t(y) * width + x) * 4, sizeof(pixel));
                        ++sampled;
                        fillPixels += pixel == expected.fill;
                        textPixels += pixel == expected.text;
                    }
            Check(sampled > 0 && fillPixels > sampled * 3 / 4 && textPixels > 20,
                  "muted card renders the exact opaque system highlight/text pair");
        }
        size_t changed = 0;
        for (size_t i = 4; i < pixels.size(); i += 4)
            if (memcmp(pixels.data(), pixels.data() + i, 4) != 0)
                ++changed;
        // Contrast mode intentionally uses one flat opaque surface; thin borders/text occupy fewer pixels than
        // decorative dark card fills. Its exact background is checked above, with a separate nonblank-content gate.
        if (changed <= size_t(width) * height / (expectedBackground ? 1000 : 20))
            std::fprintf(stderr, "Sparse AV capture %ls: %zu changed pixels of %zu\n", path.filename().c_str(), changed,
                         pixels.size() / 4);
        Check(changed > size_t(width) * height / (expectedBackground ? 1000 : 20),
              "AV screenshot contains visible control content");
        std::filesystem::create_directories(path.parent_path());
        wil::com_ptr_nothrow<IWICImagingFactory> factory;
        Hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())),
           "AV WIC");
        wil::com_ptr_nothrow<IWICStream> stream;
        Hr(factory->CreateStream(stream.put()), "AV image stream");
        Hr(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "AV image output");
        wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
        Hr(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()), "AV PNG encoder");
        Hr(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache), "AV PNG initialize");
        wil::com_ptr_nothrow<IWICBitmapFrameEncode> frame;
        Hr(encoder->CreateNewFrame(frame.put(), nullptr), "AV PNG frame");
        Hr(frame->Initialize(nullptr), "AV frame initialize");
        Hr(frame->SetSize(width, height), "AV image size");
        auto format = GUID_WICPixelFormat32bppBGRA;
        Hr(frame->SetPixelFormat(&format), "AV image format");
        Check(format == GUID_WICPixelFormat32bppBGRA, "AV image BGRA");
        Hr(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), pixels.data()), "AV image pixels");
        Hr(frame->Commit(), "AV PNG frame commit");
        Hr(encoder->Commit(), "AV PNG commit");
    }
};
struct Commands final
{
    uint32_t count = 0, frames = 0;
    uint32_t savedCount = 0;
    bool rejectSave = false;
    AVControl::Configuration saved;
    AVControl::ViewCommand last;
    AVControl::ViewCallbacks Callbacks() noexcept
    {
        return {this,
                [](void* context, const AVControl::ViewCommand& command) noexcept
                {
                    auto& self = *static_cast<Commands*>(context);
                    ++self.count;
                    self.last = command;
                },
                [](void* context) noexcept { ++static_cast<Commands*>(context)->frames; },
                [](void* context, const AVControl::Configuration& configuration) noexcept
                {
                    auto& self = *static_cast<Commands*>(context);
                    if (self.rejectSave)
                        return E_ACCESSDENIED;
                    self.saved = configuration;
                    ++self.savedCount;
                    return S_OK;
                }};
    }
};
void Click(AVControl::LiveView& view, AVControl::Rect rect)
{
    const float x = rect.x + rect.width / 2, y = rect.y + rect.height / 2;
    Check(view.Pointer({DxUi::PointerAction::Down, x, y}), "AV pointer down consumed");
    Check(view.Pointer({DxUi::PointerAction::Up, x, y}), "AV pointer up consumed");
}
DxUi::Control* FindAccessible(DxUi::Control* control, std::wstring_view name)
{
    if (!control || !control->IsVisible())
        return nullptr;
    if (control->GetAccessibleName() == name || control->GetAccessibleAutomationId() == name)
        return control;
    for (size_t i = 0; i < control->GetLogicalChildCount(); ++i)
        if (auto* found = FindAccessible(control->GetLogicalChild(i), name))
            return found;
    return nullptr;
}
void ClickNamed(AVControl::LiveView& view, std::wstring_view name)
{
    auto* control = FindAccessible(view.Controls().GetRoot(), name);
    Check(control && control->IsEnabled(), "profile action is visible and enabled");
    const auto bounds = control->GetBounds();
    Click(view, {bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top});
}
} // namespace

uint32_t RunNativeViewTests()
{
    using namespace AVControl;
    Hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "AV test COM apartment");
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    Fixture gpu;
    std::shared_ptr<DxUi::GraphicsDevice> pool;
    Hr(DxUi::GraphicsDevice::Create(gpu.device.get(), pool), "AV shares supplied device");
    LiveView view;
    Commands commands;
    Hr(view.Attach(pool, commands.Callbacks()), "AV public controls attach");
    view.SetVisible(true);
    ConfirmedState state;
    Check(state.output.id.Assign("fixture-output") && state.microphone.id.Assign("fixture-mic") &&
              state.camera.sourceId.Assign("fixture-camera"),
          "synthetic endpoint IDs");
    state.output.availability = state.microphone.availability = state.camera.availability = Availability::Ready;
    state.output.level = 65;
    state.microphone.level = 72;
    state.microphone.muted = true;
    state.output.generation = state.microphone.generation = 1;
    state.camera.revision = 1;
    view.SetState(state, L"Studio", 0);
    wchar_t executable[MAX_PATH]{};
    Check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "AV artifact location");
    const auto artifactRoot =
        std::filesystem::path(executable).parent_path().parent_path().parent_path() / L"test-artifacts" / L"AVControl";
    constexpr std::array<std::array<uint32_t, 2>, 10> dimensions{{{1280, 720},
                                                                  {640, 720},
                                                                  {1280, 360},
                                                                  {640, 360},
                                                                  {320, 720},
                                                                  {320, 360},
                                                                  {640, 180},
                                                                  {320, 180},
                                                                  {160, 180},
                                                                  {360, 2560}}};
    for (const auto& size : dimensions)
    {
        gpu.Resize(size[0], size[1]);
        Hr(view.Prepare(size[0], size[1], 96), "AV responsive native preparation");
        gpu.Draw(view);
        gpu.Save(artifactRoot / (std::to_wstring(size[0]) + L"x" + std::to_wstring(size[1]) + L".png"));
        Check(view.Prepare(size[0], size[1], 96) == S_FALSE, "AV unchanged preparation is clean");
        const auto before = commands.count;
        Click(view, view.Layout().toggles[0]);
        Check(commands.count == before + 1 && commands.last.kind == ViewCommandKind::SetMuted &&
                  commands.last.device == DeviceKind::Output && commands.last.value == 1 &&
                  commands.last.endpointId == state.output.id,
              "AV one-click mute binds exact confirmed endpoint");
        auto* outputToggle = dynamic_cast<DxUi::Toggle*>(FindAccessible(view.Controls().GetRoot(), L"av.toggle.Out"));
        Check(outputToggle && !outputToggle->IsChecked(),
              "pending pointer mute does not publish an optimistic accessible toggle state");
        Hr(view.Prepare(size[0], size[1], 96), "prepare profile target after mute input");
        const auto profileBefore = commands.count;
        Click(view, view.Layout().profileSelector);
        Check(commands.count == profileBefore + 1 && commands.last.kind == ViewCommandKind::OpenProfiles,
              "profile selection remains a direct touch action at every supported size");
    }
    gpu.Resize(640, 360);
    Hr(view.Prepare(640, 360, 96), "AV slider fixture prepare");
    auto* microphoneToggle = dynamic_cast<DxUi::Toggle*>(FindAccessible(view.Controls().GetRoot(), L"av.toggle.Mic"));
    Check(microphoneToggle && microphoneToggle->IsChecked(),
          "retained toggle state reports the confirmed microphone mute");
    const auto beforeAccessibleToggle = commands.count;
    Check(microphoneToggle->OnMnemonic(view.Controls()) && commands.count == beforeAccessibleToggle + 1 &&
              commands.last.device == DeviceKind::Microphone && commands.last.value == 0 &&
              microphoneToggle->IsChecked(),
          "accessibility toggle activation submits one command while preserving confirmed mute until readback");
    Hr(view.Prepare(640, 360, 96), "prepare after accessible toggle activation before a new hit-tested gesture");
    const auto slider = view.Layout().sliders[0];
    const float y = slider.y + slider.height / 2;
    auto count = commands.count;
    Check(view.Pointer({DxUi::PointerAction::Down, slider.x + slider.width * .25f, y}), "AV slider down");
    Check(view.Pointer({DxUi::PointerAction::Move, slider.x + slider.width * .75f, y}), "AV slider drag");
    Check(commands.count == count, "AV preview performs no endpoint mutation");
    Hr(view.Prepare(640, 360, 96), "AV local preview preparation");
    view.Pointer({DxUi::PointerAction::Up, slider.x + slider.width * .75f, y});
    Check(commands.count == count + 1 && commands.last.kind == ViewCommandKind::SetLevel && commands.last.value >= 70 &&
              commands.last.value <= 80,
          "AV slider commits once on release");
    Hr(view.Prepare(640, 360, 96), "AV next gesture preparation");
    count = commands.count;
    view.Pointer({DxUi::PointerAction::Down, slider.x + slider.width * .2f, y});
    view.Cancel();
    view.Pointer({DxUi::PointerAction::Up, slider.x + slider.width * .8f, y});
    Check(commands.count == count, "AV canceled gesture never commits");
    Hr(view.Prepare(640, 360, 96), "AV external-change gesture prepare");
    view.Pointer({DxUi::PointerAction::Down, slider.x + slider.width * .2f, y});
    ++state.output.levelRevision;
    state.output.level = 22;
    view.SetState(state, L"Studio · adjusted", 0);
    view.Pointer({DxUi::PointerAction::Up, slider.x + slider.width * .8f, y});
    Check(commands.count == count, "AV external level notification cancels stale draft");
    Hr(view.Prepare(640, 360, 96), "AV confirmed change prepare");
    gpu.Draw(view);
    const auto initial = view.Statistics();
    for (uint32_t i = 0; i < 1000; ++i)
        gpu.Draw(view);
    const auto steady = view.Statistics();
    Check(steady.preparations == initial.preparations && steady.surfaceAllocations == initial.surfaceAllocations &&
              steady.composites == initial.composites + 1000,
          "AV 1000 steady frames reuse the prepared surface");
    Check(view.Composite(gpu.context.get(), {-80, 0, 640, 360, 0, 1}) == S_OK,
          "AV supports partially offscreen page swipe");
    view.SetVisible(false);
    const auto hidden = view.Statistics();
    Check(view.Prepare(640, 360, 96) == S_FALSE && !view.Pointer({DxUi::PointerAction::Down, 20, 90}),
          "AV hidden view does no preparation or input");
    Check(view.Statistics().preparations == hidden.preparations, "AV hidden preparation count unchanged");
    view.SetVisible(true);
    Hr(view.Prepare(640, 360, 96), "AV visible restore");
    state.output.availability = Availability::Missing;
    view.SetState(state, L"Custom", 0);
    Hr(view.Prepare(640, 360, 96), "AV missing endpoint state");
    count = commands.count;
    view.Pointer({DxUi::PointerAction::Down, 30, 100});
    view.Pointer({DxUi::PointerAction::Up, 30, 100});
    Check(commands.count == count, "AV unavailable control performs no mutation");
    for (auto unavailable : {Availability::AccessDenied, Availability::Busy, Availability::Failed})
    {
        state.camera.availability = unavailable;
        state.camera.enabled = false;
        view.SetState(state, L"Custom", 0);
        Hr(view.Prepare(640, 360, 96), "AV camera retry state");
        const auto prior = commands.count;
        auto* retry = FindAccessible(view.Controls().GetRoot(), L"av.toggle.Cam");
        Check(retry && retry->IsEnabled(), "transient camera failures expose explicit retry");
        const auto bounds = retry->GetBounds();
        view.Pointer({DxUi::PointerAction::Down, (bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2});
        view.Pointer({DxUi::PointerAction::Up, (bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2});
        Check(commands.count == prior + 1 && commands.last.kind == ViewCommandKind::SetCameraEnabled &&
                  commands.last.value == 1,
              "camera retries only after an explicit activation and preserves revision-bound intent");
    }
    state.camera.availability = Availability::Ready;
    RedXeAppearance appearance;
    appearance.flags = RedXeAppearanceHighContrast;
    appearance.window = 0xffffffff;
    appearance.windowText = 0xff000000;
    appearance.highlight = 0xff000080;
    appearance.highlightText = 0xffffffff;
    appearance.button = 0xffffffff;
    appearance.buttonText = 0xff000000;
    appearance.disabledText = 0xff606060;
    view.SetAppearance(appearance);
    Hr(view.Prepare(640, 360, 96), "AV applies host-supplied contrast palette during preparation");
    const auto& contrast = view.Controls().GetTheme();
    Check(contrast.highContrast && contrast.reducedMotion && contrast.windowBackground.r == 1 && contrast.text.r == 0 &&
              contrast.selectionFill.b > .49f && contrast.selectionFill.r == 0,
          "AV respects exact contrast colors without decorative surface blends");
    gpu.Draw(view);
    gpu.Save(artifactRoot / L"high-contrast.png", 0xffffffff,
             Fixture::AccentCheck{view.Layout().toggles[1], appearance.highlight, appearance.highlightText});
    const auto themed = view.Statistics();
    view.SetAppearance(appearance);
    Check(view.Prepare(640, 360, 96) == S_FALSE && view.Statistics().preparations == themed.preparations,
          "unchanged appearance does not schedule repeated raster preparation");
    appearance.flags = 0;
    view.SetAppearance(appearance);
    Hr(view.Prepare(640, 360, 96), "AV returns to light appearance without device recreation");
    Check(!view.Controls().GetTheme().dark && !view.Controls().GetTheme().highContrast,
          "AV applies normal light appearance");
    gpu.Draw(view);
    gpu.Save(artifactRoot / L"light.png");
    view.SetAppearance({}); // Keep the independent profile-editor captures in their original dark fixture.
    view.Detach();

    // Exercise the actual native controls and callbacks, including typed text and save-only profile semantics.
    Hr(view.Attach(pool, commands.Callbacks()), "profile view reattaches after device lifetime");
    view.SetVisible(true);
    auto inventory = std::make_unique<Inventory>();
    inventory->state = state;
    inventory->state.output.availability = Availability::Ready;
    inventory->outputs[0] = inventory->state.output;
    inventory->inputs[0] = inventory->state.microphone;
    inventory->cameras[0] = {state.camera.sourceId, {}, Availability::Ready};
    inventory->outputCount = inventory->inputCount = inventory->cameraCount = 1;
    Configuration configuration;
    configuration.count = 1;
    auto& profile = configuration.profiles[0];
    Check(profile.id.Assign("studio") && profile.name.Assign("Studio"), "native profile identity");
    profile.outputId = state.output.id;
    profile.microphoneId = state.microphone.id;
    profile.cameraId = state.camera.sourceId;
    view.SetProfiles(configuration, inventory.get(), 1);
    view.ShowProfiles();
    gpu.Resize(1280, 720);
    auto prepare = [&] { Hr(view.Prepare(gpu.width, gpu.height, 96), "profile native preparation"); };
    prepare();
    gpu.Draw(view);
    gpu.Save(artifactRoot / L"profiles-chooser.png");
    inventory->truncated = true;
    view.SetProfiles(configuration, inventory.get(), 2);
    prepare();
    auto* overflow = dynamic_cast<DxUi::Label*>(FindAccessible(
        view.Controls().GetRoot(),
        L"Device list is limited. Defaults and saved profile bindings are shown first. Some devices may be omitted."));
    Check(overflow && overflow->GetText().find(L"Limited devices") != std::wstring_view::npos,
          "inventory overflow has a visible heading and full accessible explanation");
    gpu.Draw(view);
    gpu.Save(artifactRoot / L"profiles-limited-devices.png");
    gpu.Resize(160, 180);
    prepare();
    Check(FindAccessible(view.Controls().GetRoot(), L"Device list is limited. Defaults and saved profile bindings are "
                                                    L"shown first. Some devices may be omitted.") != nullptr,
          "minimum profile view retains an explicit overflow notice");
    Check(overflow->GetText() == L"Limited", "minimum overflow caption fits beside the 48-DIP Back target");
    gpu.Draw(view);
    gpu.Save(artifactRoot / L"profiles-limited-minimum.png");
    inventory->truncated = false;
    view.SetProfiles(configuration, inventory.get(), 3);
    gpu.Resize(1280, 720);
    prepare();
    count = commands.count;
    ClickNamed(view, L"Apply profile Studio");
    Check(commands.count == count + 1 && commands.last.kind == ViewCommandKind::ApplyProfile &&
              commands.last.value == 0 && !view.ProfilesVisible(),
          "profile applies exactly once and returns to live controls");
    prepare();
    view.ShowProfiles();
    prepare();
    ClickNamed(view, L"Define profiles");
    prepare();
    ClickNamed(view, L"Create a new profile");
    prepare();
    // AV applies the public 48-DIP row minimum to both device bindings and role scope. Exercise the actual
    // popup row geometry and a click near its bottom edge, where the former 24/32-DIP hit layout disagreed.
    for (auto caption : {L"Audio output", L"Microphone", L"Camera source", L"Audio role scope"})
    {
        auto* combo = dynamic_cast<DxUi::ComboBox*>(FindAccessible(view.Controls().GetRoot(), caption));
        Check(combo && combo->GetMinimumPopupItemHeight() == 48.0f, "AV selector opts into touch-sized popup rows");
        const auto field = combo->GetBounds();
        Check(view.Pointer({DxUi::PointerAction::Down, (field.left + field.right) / 2, (field.top + field.bottom) / 2}),
              "AV selector consumes popup-open press");
        // ComboBox opens on Down; its non-dragging Up has no separate control action.
        view.Pointer({DxUi::PointerAction::Up, (field.left + field.right) / 2, (field.top + field.bottom) / 2});
        prepare();
        Check(combo->IsPopupOpen(), "AV device/role selector opens through live pointer input");
        const auto row = combo->DebugGetPopupItemRect(0, &view.Controls());
        Check(row.bottom - row.top >= 48.0f, "AV selector actually lays out 48-DIP popup rows");
        const auto bounds = combo->DebugGetPopupBounds();
        Check(view.Pointer({DxUi::PointerAction::Wheel, (bounds.left + bounds.right) / 2,
                            (bounds.top + bounds.bottom) / 2, 0, -120.0f}),
              "AV popup consumes routed vertical wheel input");
        prepare();
        view.Pointer({DxUi::PointerAction::Down, row.left + 10, row.bottom - 2});
        view.Pointer({DxUi::PointerAction::Up, row.left + 10, row.bottom - 2});
        prepare();
        Check(!combo->IsPopupOpen() && combo->GetSelectedIndex() == 0, "bottom of touch row selects its visible item");
    }
    auto* name = dynamic_cast<DxUi::TextField*>(FindAccessible(view.Controls().GetRoot(), L"av.profile.name"));
    Check(name != nullptr, "profile editor exposes public text field");
    view.Controls().SetFocusControl(name);
    Check(view.Key('A', true, MK_CONTROL), "profile name supports select-all keyboard command");
    for (wchar_t character : std::wstring_view(L"Travel"))
        Check(view.Character(character), "profile text input accepted");
    Check(name->GetText() == L"Travel", "profile name typed through embedded text input");
    auto text = std::make_unique<RedXeTextState>();
    Check(view.ReadText(*text) == S_OK && text->focusId && text->textLength == 6,
          "AV exposes a focused bounded text session");
    const auto firstTextFocus = text->focusId;
    const auto previewName = [&](std::wstring_view value)
    {
        Check(view.ReadText(*text) == S_OK, "read current profile text before composition");
        DxUi::NativeTextInputState next;
        Check(RedXeTextTransport::DecodeTextState(*text, next) == S_OK, "decode profile text into local service state");
        next.text = value;
        next.caretIndex = value.size();
        next.selectionAnchorIndex.reset();
        next.compositionStartIndex = 0;
        next.compositionEndIndex = value.size();
        next.compositionCursorIndex = value.size();
        const uint64_t revision = text->revision, focus = text->focusId;
        Check(RedXeTextTransport::EncodeTextState(revision, focus, next, {}, {}, 96, *text) == S_OK &&
                  view.ApplyText(RedXeTextPreview, *text) == S_OK,
              "AV applies composition preview through generic transport");
    };
    previewName(L"Travel \u6771\u4eac");
    Check(name->GetText() == L"Travel \u6771\u4eac" && commands.savedCount == 0,
          "IME preview remains local to the unsaved editor");
    Check(view.ApplyText(RedXeTextPreview, *text) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
          "late text edit cannot replay a consumed revision");
    Check(view.Key(VK_ESCAPE, true) && view.ProfilesVisible() && name->GetText() == L"Travel",
          "first Escape cancels composition before navigating out of the profile editor");
    prepare();
    Check(view.ReadText(*text) == S_OK && text->focusId == firstTextFocus && (text->flags & RedXeTextViewportBounds),
          "composition cancellation preserves focus and prepared physical geometry");
    RedXeTextRectangle textBounds;
    BOOL textClipped = TRUE;
    uint32_t textIndex = UINT32_MAX;
    Check(view.TextRange(text->revision, 0, 4, textBounds, textClipped) == S_OK && textBounds.right > textBounds.left,
          "profile range exposes prepared physical geometry");
    Check(view.HitText(text->revision, textBounds.left, textBounds.top + 1, textIndex) == S_OK && textIndex <= 1,
          "profile hit test uses the same physical space");
    view.Controls().SetFocusControl(nullptr);
    view.Controls().SetFocusControl(name);
    previewName(L"Replacement focus draft");
    Check(view.CancelText(firstTextFocus) == S_FALSE && name->GetText() == L"Replacement focus draft",
          "old TSF context cannot cancel a replacement focus session");
    Check(view.ReadText(*text) == S_OK && view.CancelText(text->focusId) == S_OK && name->GetText() == L"Travel",
          "current focus cancellation restores the original draft");
    previewName(L"Uncommitted device-loss preview");
    // A device-loss rebuild destroys every control/resource but must keep the unsaved name and bindings.
    view.Detach();
    Hr(view.Attach(pool, commands.Callbacks()), "profile draft reattaches after device loss");
    view.SetVisible(true);
    prepare();
    name = dynamic_cast<DxUi::TextField*>(FindAccessible(view.Controls().GetRoot(), L"av.profile.name"));
    Check(view.ProfilesVisible() && name && name->GetText() == L"Travel" && commands.savedCount == 0,
          "device loss preserves the unsaved profile editor without persisting or applying it");
    prepare();
    gpu.Draw(view);
    gpu.Save(artifactRoot / L"profiles-editor.png");
    count = commands.count;
    ClickNamed(view, L"Save profile definition without changing devices");
    prepare();
    Check(commands.savedCount == 1 && commands.saved.count == 2 && commands.saved.profiles[1].name.View() == "Travel" &&
              commands.count == count,
          "Save persists only definitions and performs no device/apply command");
    Check(FindAccessible(view.Controls().GetRoot(), L"av.profile.name") == nullptr,
          "hidden editor is absent from visible focus tree");
    ClickNamed(view, L"Define profiles");
    prepare();
    ClickNamed(view, L"Edit profile Travel");
    prepare();
    name = dynamic_cast<DxUi::TextField*>(FindAccessible(view.Controls().GetRoot(), L"av.profile.name"));
    Check(name != nullptr, "saved profile returns to editor");
    name->SetText(L"Discarded draft");
    Check(view.Key(VK_ESCAPE, true), "Escape leaves editor");
    prepare();
    Check(commands.savedCount == 1 && FindAccessible(view.Controls().GetRoot(), L"Apply profile Travel"),
          "Cancel discards profile draft");
    ClickNamed(view, L"Define profiles");
    prepare();
    ClickNamed(view, L"Edit profile Travel");
    prepare();
    commands.rejectSave = true;
    ClickNamed(view, L"Save profile definition without changing devices");
    prepare();
    Check(commands.savedCount == 1 && commands.count == count,
          "failed profile save performs no apply and preserves definitions");
    Check(view.Key(VK_ESCAPE, true), "save failure can return to existing draft");
    prepare();
    Check(FindAccessible(view.Controls().GetRoot(), L"av.profile.name") != nullptr,
          "save failure leaves draft repairable");
    commands.rejectSave = false;
    gpu.Resize(160, 180);
    prepare();
    gpu.Draw(view);
    gpu.Save(artifactRoot / L"profiles-editor-minimum.png");
    // This is the same view/surface at a different size; form fields page without scrolling or smaller targets.
    auto* initialField = FindAccessible(view.Controls().GetRoot(), L"av.profile.name");
    Check(initialField && initialField->GetBounds().bottom - initialField->GetBounds().top >= 48,
          "minimum editor keeps a full-size field in a bounded step");
    view.CloseProfiles();
    view.SetProfiles(Configuration{}, inventory.get(), 4);
    const auto guideCommands = commands.count, guideSaves = commands.savedCount;
    view.ShowProfiles();
    prepare();
    ClickNamed(view, L"Camera setup guide");
    prepare();
    for (uint32_t step = 1; step <= 6; ++step)
    {
        Check(FindAccessible(view.Controls().GetRoot(), std::wstring(L"Camera setup, step ") + std::to_wstring(step) + L" of 6") != nullptr,
            "camera setup exposes a numbered accessible step");
        gpu.Draw(view);
        gpu.Save(artifactRoot / (L"camera-setup-minimum-" + std::to_wstring(step) + L".png"));
        auto* done = FindAccessible(view.Controls().GetRoot(), L"Return to AV profiles without changing devices");
        Check(done && done->GetBounds().right - done->GetBounds().left >= 48 && done->GetBounds().bottom - done->GetBounds().top >= 48,
            "camera guide retains a full-size return target at minimum dimensions");
        if (step < 6) { ClickNamed(view, L"Next"); prepare(); }
    }
    gpu.Resize(1280, 720);
    prepare();
    gpu.Draw(view);
    gpu.Save(artifactRoot / L"camera-setup-full.png");
    ClickNamed(view, L"Return to AV profiles without changing devices");
    prepare();
    Check(FindAccessible(view.Controls().GetRoot(), L"Camera setup guide") && commands.count == guideCommands && commands.savedCount == guideSaves,
        "camera guidance never registers a device, changes audio or saves profiles");
    view.Detach();
    return nativeChecks;
}
