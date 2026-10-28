#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"
#include "ProcessViewer.Tests.Contract.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
// Only the explicit pixel-reference test mode permits the preceding implementation without Prepare.
std::ofstream g_pixelReference;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class ObservationSubscription final : public RedXeComObject<ObservationSubscription, IRedXeDataSubscription>
{
  public:
    explicit ObservationSubscription(IRedXeDataSink* value) noexcept : sink(value) {}
    HRESULT STDMETHODCALLTYPE SetActive(BOOL value) noexcept override
    {
        active = value != FALSE;
        return S_OK;
    }
    wil::com_ptr_nothrow<IRedXeDataSink> sink;
    bool active = false;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class ObservationProvider final : public RedXeComObject<ObservationProvider, IRedXeDataProvider>
{
  public:
    HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors, uint32_t* count) noexcept override
    {
        if (!descriptors || !count)
            return E_POINTER;
        *descriptors = nullptr;
        *count = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Subscribe(const RedXeDataSubscriptionOptions* options, IRedXeDataSink* sink,
                                        IRedXeDataSubscription** output) noexcept override
    {
        if (!output)
            return E_POINTER;
        *output = nullptr;
        if (!options || !sink || slotCount == slots.size())
            return E_INVALIDARG;
        auto& slot = slots[slotCount++];
        slot.id = options->dataSetId;
        slot.subscription.attach(new (std::nothrow) ObservationSubscription(sink));
        if (!slot.subscription)
            return E_OUTOFMEMORY;
        return slot.subscription->QueryInterface(__uuidof(IRedXeDataSubscription), reinterpret_cast<void**>(output));
    }
    HRESULT Deliver(const RedXeDataSnapshot& snapshot)
    {
        HRESULT result = S_FALSE;
        // Exercise the real DLL's worker/UI boundary. Each call joins before changing borrowed fixture storage.
        std::jthread worker(
            [&]
            {
                for (auto& slot : slots)
                    if (slot.subscription && slot.subscription->active && std::strcmp(slot.id, snapshot.dataSetId) == 0)
                        result = slot.subscription->sink->OnDataSnapshot(&snapshot);
            });
        worker.join();
        return result;
    }

  private:
    struct Slot final
    {
        const char* id = nullptr;
        wil::com_ptr_nothrow<ObservationSubscription> subscription;
    };
    std::array<Slot, 2> slots{};
    size_t slotCount = 0;
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class ObservationHost final : public RedXeComObject<ObservationHost, IRedXeHost>
{
  public:
    ObservationHost()
    {
        data.attach(new ObservationProvider());
    }
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char*, IRedXeDataProvider** output) noexcept override
    {
        return data->QueryInterface(__uuidof(IRedXeDataProvider), reinterpret_cast<void**>(output));
    }
    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override
    {
        ++requests;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char*, const RedXeWidgetStatusReport*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char*, const char*, uint32_t) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueueConstructionWork(IRedXeConstructionWork*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ExecuteAction(const RedXeActionRequest*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ValidateAction(const RedXeActionRequest*, const RedXeActionDescriptor**) noexcept override
    {
        return E_NOTIMPL;
    }
    wil::com_ptr_nothrow<ObservationProvider> data;
    std::atomic<uint32_t> requests{0};
};
#pragma warning(pop)

struct Observation final
{
    std::array<std::array<RedXeDataValue, 22>, 2> values{};
    std::array<RedXeDataRow, 2> rows{};
    RedXeDataSnapshot snapshot{};
    Observation(const Observation&) = delete;
    Observation& operator=(const Observation&) = delete;
    Observation(const char* id, uint32_t columns, uint32_t rowCount = 1)
    {
        for (size_t row = 0; row < rows.size(); ++row)
        {
            for (auto& value : values[row])
                value = {sizeof(RedXeDataValue), RedXeDataValueTypeUInt64, RedXeDataQualityGood, {0}, 0};
            rows[row] = {sizeof(RedXeDataRow), values[row].data(), columns};
        }
        snapshot = {sizeof(RedXeDataSnapshot), 0, id, 1, 1, rows.data(), rowCount, columns};
    }
    void Number(uint32_t column, double number, uint32_t row = 0)
    {
        auto& value = values[row][column];
        value.valueType = RedXeDataValueTypeFloat64;
        value.float64Value = number;
    }
    void Integer(uint32_t column, uint64_t number, uint32_t row = 0)
    {
        values[row][column].uint64Value = number;
    }
    void Text(uint32_t column, const wchar_t* text, uint32_t row = 0)
    {
        auto& value = values[row][column];
        value.valueType = RedXeDataValueTypeUtf16;
        value.utf16Value = text;
        value.utf16Characters = static_cast<uint32_t>(wcslen(text));
    }
};

struct Surface final
{
    static constexpr uint32_t width = 640, height = 360;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture, readback;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> target;
    Surface()
    {
        Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                            device.put(), nullptr, context.put())),
                "viewer WARP device failed");
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BindFlags = D3D11_BIND_RENDER_TARGET;
        Require(SUCCEEDED(device->CreateTexture2D(&description, nullptr, texture.put())) &&
                    SUCCEEDED(device->CreateRenderTargetView(texture.get(), nullptr, target.put())),
                "viewer target failed");
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Require(SUCCEEDED(device->CreateTexture2D(&description, nullptr, readback.put())), "viewer readback failed");
    }
    uint64_t Pixels()
    {
        context->CopyResource(readback.get(), texture.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(context->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped)), "viewer pixel map failed");
        const auto unmap = wil::scope_exit([&] { context->Unmap(readback.get(), 0); });
        uint64_t hash = 14695981039346656037ull;
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width * 4; ++x)
            {
                hash ^= static_cast<const uint8_t*>(mapped.pData)[y * mapped.RowPitch + x];
                hash *= 1099511628211ull;
            }
        return hash;
    }
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct Viewer final
{
    ObservationHost host;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    wil::com_ptr_nothrow<IRedXeWidget> widget;
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpu;
    wil::com_ptr_nothrow<IRedXeScheduledWidget> scheduled;
    wil::com_ptr_nothrow<IRedXePreparedGpuWidget> prepared;
    Surface& surface;
    Viewer(RedXeCreateFn create, Surface& surfaceValue, const char* id, const char* type, const char* config = nullptr,
           bool initializeGpu = true)
        : surface(surfaceValue)
    {
        RedXeFactoryOptions options{sizeof(options)};
        options.configurationJsonUtf8 = config;
        options.configurationBytes = config ? static_cast<uint32_t>(std::strlen(config)) : 0;
        void* object = nullptr;
        Require(SUCCEEDED(create(__uuidof(IRedXeWidgetProvider), &options, &host, id, &object)),
                "viewer factory failed");
        provider.attach(static_cast<IRedXeWidgetProvider*>(object));
        Require(SUCCEEDED(provider->CreateWidget(type, "schedule.fixture", widget.put())) &&
                    SUCCEEDED(widget.query_to(gpu.put())) && SUCCEEDED(widget.query_to(scheduled.put())),
                "viewer interfaces failed");
        const HRESULT preparation = widget.query_to(prepared.put());
        Require(SUCCEEDED(preparation) || g_pixelReference.is_open(), "viewer preparation interface missing");
        _type = type;
        if (initializeGpu)
        {
            const RedXeGpuDeviceContext device{sizeof(device), surface.device.get(), DXGI_FORMAT_B8G8R8A8_UNORM,
                                               D3D_FEATURE_LEVEL_11_0};
            Require(SUCCEEDED(gpu->OnDeviceCreated(&device)), "viewer device setup failed");
            Resize(Surface::width, Surface::height);
            Require(SUCCEEDED(widget->SetVisible(TRUE)), "viewer show failed");
        }
    }
    ~Viewer()
    {
        if (widget)
            (void)widget->SetVisible(FALSE);
        if (gpu)
            gpu->OnDeviceLost();
    }
    void Resize(uint32_t width, uint32_t height)
    {
        const RedXeGpuTargetSizeContext size{sizeof(size), width, height, 96};
        Require(gpu->OnTargetSizeChanged(&size) == S_OK, "viewer size failed");
    }
    void Send(Observation& observation)
    {
        ++observation.snapshot.sequence;
        ++observation.snapshot.timestampFileTime100ns;
        Require(host.data->Deliver(observation.snapshot) == S_OK, "viewer snapshot failed");
    }
    HRESULT Prepare() noexcept
    {
        if (!prepared && g_pixelReference.is_open())
            return S_FALSE;
        const RedXeGpuPreparationContext context{sizeof(context), 96, Surface::width, Surface::height, 0, 0, {}};
        return prepared->Prepare(&context);
    }
    HRESULT Draw(float now, uint32_t view = 0, float delta = 1.0f / 60.0f, bool prepare = true) noexcept
    {
        if (prepare)
        {
            const HRESULT result = Prepare();
            if (FAILED(result))
                return result;
        }
        auto* target = surface.target.get();
        surface.context->OMSetRenderTargets(1, &target, nullptr);
        const float black[4] = {0, 0, 0, 1};
        surface.context->ClearRenderTargetView(target, black);
        const RedXeWidgetFrameContext frame{sizeof(frame), Surface::width, Surface::height, 96, now, delta};
        const RedXeGpuFrameContext context{
            sizeof(context),
            &frame,
            surface.context.get(),
            {0, 0, static_cast<float>(Surface::width), static_cast<float>(Surface::height), 0, 1},
            view};
        surface.context->RSSetViewports(1, &context.viewport);
        const HRESULT result = gpu->Render(&context);
        if (SUCCEEDED(result) && g_pixelReference.is_open())
            g_pixelReference << _type << ' ' << now << ' ' << view << ' ' << surface.Pixels() << '\n';
        return result;
    }
    bool Moving()
    {
        uint32_t delay = 123;
        const HRESULT result = scheduled->GetNextFrameDelayMilliseconds(&delay);
        Require((result == S_FALSE && delay == 0) || (result == S_OK && delay == 33), "viewer deadline is invalid");
        return result == S_OK;
    }
    const char* _type = nullptr;
};
#pragma warning(pop)

void FillSummary(Observation& observation, double cpu)
{
    observation.Number(0, cpu);
    observation.Integer(1, 32);
    observation.Integer(2, 200);
    observation.Integer(3, 4000);
    observation.Integer(4, 10000);
    observation.Integer(5, 64'000'000'000);
    observation.Integer(7, 20'000'000'000);
    observation.Integer(8, 24'000'000'000);
    observation.Integer(9, 3'600'000);
}

// Observation holds pointers to its own storage; keep named fixtures in place (rather than copying them).
void CheckCadence(RedXeCreateFn create, Surface& surface)
{
    Viewer viewer(create, surface, "builtin.system-pulse", "system-pulse");
    Require(viewer.scheduled->GetNextFrameDelayMilliseconds(nullptr) == E_POINTER, "viewer accepted null deadline");
    Observation sample("system.summary", 10);
    FillSummary(sample, 10);
    viewer.Send(sample);
    Require(viewer.Draw(1) == S_OK && !viewer.Moving(), "first observation did not snap and park");
    const uint64_t initial = surface.Pixels();
    for (uint32_t observation = 0; observation < 12; ++observation)
        viewer.Send(sample);
    Require(viewer.Draw(2, 0, 1) == S_OK && !viewer.Moving() && surface.Pixels() == initial,
            "identical snapshot burst changed pixels or scheduled an idle ease");

    sample.Number(0, 60);
    viewer.Send(sample);
    Require(viewer.Draw(3, 0, 1) == S_OK && viewer.Moving() && surface.Pixels() == initial,
            "changed value skipped its ease after a long static interval");
    Require(viewer.Draw(3.1f) == S_OK && viewer.Moving(), "visible ease stopped early");
    const uint64_t middle = surface.Pixels();
    Require(middle != initial && viewer.Draw(3.1f, 1) == S_OK && surface.Pixels() == middle,
            "tile and raised draw advanced one animation twice");
    viewer.Send(sample);
    Require(viewer.Draw(3.33f) == S_OK && !viewer.Moving(), "identical sample restarted an active ease");
    const uint64_t final = surface.Pixels();
    Require(final != initial && final != middle, "visible easing lost its final target");
    Require(viewer.Draw(5, 0, 1.67f) == S_OK && surface.Pixels() == final && !viewer.Moving(),
            "settled values changed or introduced an idle deadline");

    sample.Number(0, 60.02);
    viewer.Send(sample);
    Require(viewer.Draw(6) == S_OK && !viewer.Moving(), "sub-pixel observation started an ease");
    viewer.Resize(5000, 720);
    sample.Number(0, 60.05);
    viewer.Send(sample);
    Require(viewer.Draw(7) == S_OK && viewer.Moving(), "largest raised extent was ignored for the pixel threshold");
    Require(viewer.Draw(7.33f) == S_OK && !viewer.Moving(), "size-sensitive ease did not complete");
    viewer.Resize(Surface::width, Surface::height);

    sample.Number(0, 90);
    viewer.Send(sample);
    Require(viewer.Draw(8) == S_OK && viewer.Moving() && viewer.Draw(8.34f) == S_OK && viewer.Moving(),
            "high-band pulse was lost when the value ease completed");
    const uint64_t pulsing = surface.Pixels();
    Require(viewer.Draw(8.45f) == S_OK && !viewer.Moving() && surface.Pixels() != pulsing,
            "420ms pulse did not end with the same high-band value");
    viewer.Send(sample);
    Require(viewer.Draw(9) == S_OK && viewer.Moving() && viewer.Draw(9.45f) == S_OK && !viewer.Moving(),
            "unchanged high-band sample did not retain its bounded warning pulse");

    viewer.Send(sample);
    sample.Number(0, 20);
    viewer.Send(sample);
    Require(viewer.Draw(9.5f) == S_OK && viewer.Moving() && viewer.Draw(9.84f) == S_OK && !viewer.Moving(),
            "superseded high-band observation replayed a stale pulse");

    sample.Number(0, 70);
    viewer.Send(sample);
    Require(viewer.Draw(10) == S_OK && viewer.Moving(), "changed sample did not start before hide");
    Require(viewer.widget->SetVisible(FALSE) == S_OK && !viewer.Moving() &&
                viewer.host.data->Deliver(sample.snapshot) == S_FALSE,
            "hide left work or a subscription active");
    Require(viewer.widget->SetVisible(TRUE) == S_OK && viewer.Draw(30) == S_OK && !viewer.Moving(),
            "reentry replayed interrupted motion");
    Require(viewer.Draw(31) == S_OK && !viewer.Moving(), "reentry left an idle deadline");
    const uint64_t recovered = surface.Pixels();
    Viewer reference(create, surface, "builtin.system-pulse", "system-pulse");
    reference.Send(sample);
    Require(reference.Draw(31) == S_OK && !reference.Moving() && surface.Pixels() == recovered,
            "recovery endpoint differs from a fresh exact-value view");
    sample.Number(0, 40);
    viewer.Send(sample);
    Require(viewer.Draw(32) == S_OK && viewer.Moving() && reference.Draw(32) == S_OK && !reference.Moving(),
            "independent viewer instances share animation state");
    Require(viewer.host.requests == 0, "Render or scheduling requested immediate animation frames");
}

void CheckRanksAndAuxiliaryData(RedXeCreateFn create, Surface& surface)
{
    Viewer viewer(create, surface, "builtin.process-viewer", "process-viewer");
    Observation processes("process.activity", 7, 2);
    for (uint32_t row = 0; row < 2; ++row)
    {
        processes.Integer(0, 100 + row, row);
        processes.Text(1, row == 0 ? L"Alpha" : L"Beta", row);
        processes.Number(2, row == 0 ? 20 : 10, row);
        processes.Integer(3, 20'000'000, row);
    }
    viewer.Send(processes);
    Require(viewer.Draw(1) == S_OK && !viewer.Moving(), "ranked bootstrap did not park");
    const uint64_t first = surface.Pixels();
    processes.Number(2, 30, 1);
    viewer.Send(processes);
    Require(viewer.Draw(2) == S_OK && viewer.Moving() && viewer.Draw(2.15f) == S_OK && viewer.Moving(),
            "rank change lost its row/value motion");
    processes.Text(1, L"Renamed", 1);
    viewer.Send(processes);
    Require(viewer.Draw(2.33f) == S_OK && !viewer.Moving() && surface.Pixels() != first,
            "label update restarted rank motion or discarded the changed ranking");
    const uint64_t ranked = surface.Pixels();
    processes.Integer(3, 40'000'000, 1);
    viewer.Send(processes);
    Require(viewer.Draw(3) == S_OK && !viewer.Moving() && surface.Pixels() != ranked,
            "changed working-set label was lost or caused an empty ease");

    Viewer cpu(create, surface, "builtin.cpu-meter", "cpu-meter");
    Observation summary("cpu.summary", 4);
    summary.Number(0, 10);
    summary.Number(1, 3);
    summary.Number(2, 7);
    summary.Number(3, 90);
    Observation logical("cpu.logical", 10, 2);
    logical.Number(9, 3);
    logical.Number(9, 3, 1);
    cpu.Send(summary);
    cpu.Send(logical);
    Require(cpu.Draw(4) == S_OK && !cpu.Moving(), "CPU bootstrap did not park");
    summary.Number(0, 50);
    summary.Number(1, 30);
    summary.Number(2, 20);
    cpu.Send(summary);
    Require(cpu.Draw(5) == S_OK && cpu.Moving(), "CPU value change did not animate");
    cpu.Send(logical);
    Require(cpu.Draw(5.15f) == S_OK && cpu.Moving(), "second dataset stopped the CPU ease");
    cpu.Send(logical);
    Require(cpu.Draw(5.33f) == S_OK && !cpu.Moving(), "second dataset restarted an unchanged CPU target");
    logical.Number(9, 3.01);
    cpu.Send(logical);
    Require(cpu.Draw(6) == S_OK && !cpu.Moving(), "imperceptible heat colour started an ease");
    logical.Number(9, 60);
    cpu.Send(logical);
    Require(cpu.Draw(7) == S_OK && cpu.Moving() && cpu.Draw(7.33f) == S_OK && !cpu.Moving(),
            "visible heat colour change lost its ease");
    Require(cpu.host.requests == 0 && viewer.host.requests == 0, "snapshot easing added unbounded frame requests");
}

void CheckIdleProcessPulse(RedXeCreateFn create, Surface& surface)
{
    Viewer viewer(create, surface, "builtin.process-viewer", "process-viewer",
                  R"json({"plugin":{},"instance":{"topN":10,"hideIdle":false}})json");
    Observation sample("process.activity", 7);
    sample.Integer(0, 0);
    sample.Text(1, L"System Idle Process");
    sample.Number(2, 96);
    viewer.Send(sample);
    Require(viewer.Draw(1) == S_OK && !viewer.Moving(), "System Idle Process started a warning pulse");
    sample.snapshot.rowCount = 2;
    sample.Integer(0, 100, 1);
    sample.Text(1, L"Busy process", 1);
    sample.Number(2, 88, 1);
    viewer.Send(sample);
    Require(viewer.Draw(2) == S_OK && viewer.Moving() && viewer.Draw(2.45f) == S_OK && !viewer.Moving(),
            "listed idle process suppressed a genuine busy-process pulse");
}

#if defined(_DEBUG)
std::atomic<DWORD> renderThread{0};
std::atomic<uint32_t> allocations{0};
int __cdecl CountAllocation(int type, void*, size_t, int, long, const unsigned char*, int) noexcept
{
    if (type != _CRT_BLOCK && renderThread == GetCurrentThreadId())
        ++allocations;
    return TRUE;
}
#endif

void CheckAllocationAndDeviceRecovery(RedXeCreateFn create, Surface& surface)
{
    Viewer viewer(create, surface, "builtin.system-pulse", "system-pulse");
    Observation sample("system.summary", 10);
    sample.Number(0, 30);
    viewer.Send(sample);
    Require(viewer.Draw(1) == S_OK, "allocation fixture warmup failed");
    for (uint32_t index = 0; index < 64; ++index)
        Require(viewer.Draw(2 + static_cast<float>(index) / 60) == S_OK, "render warmup failed");
#if defined(_DEBUG)
    sample.Number(0, 60);
    viewer.Send(sample);
    {
        allocations = 0;
        renderThread = GetCurrentThreadId();
        const auto previous = _CrtSetAllocHook(CountAllocation);
        const auto restore = wil::scope_exit(
            [&]
            {
        // The CRT return type erases noexcept; restore the exact previous hook unchanged.
        // Owner: RedXe test maintainers; review: 2027-01-31.
#pragma warning(suppress : 5039)
                (void)_CrtSetAllocHook(previous);
                renderThread = 0;
            });
        for (uint32_t index = 0; index < 120; ++index)
            Require(viewer.Draw(4 + static_cast<float>(index / 2) / 60, index % 2) == S_OK,
                    "cached tile/raised draw or scheduling failed");
        (void)viewer.Moving();
        Require(allocations == 0, "steady viewer Render or scheduling allocated CRT memory");
    }
#endif
    Require(viewer.widget->SetVisible(FALSE) == S_OK, "pre-recovery hide failed");
    viewer.gpu->OnDeviceLost();
    const RedXeGpuDeviceContext device{sizeof(device), surface.device.get(), DXGI_FORMAT_B8G8R8A8_UNORM,
                                       D3D_FEATURE_LEVEL_11_0};
    Require(viewer.gpu->OnDeviceCreated(&device) == S_OK && viewer.widget->SetVisible(TRUE) == S_OK &&
                viewer.Draw(50) == S_OK && !viewer.Moving(),
            "device recovery replayed stale motion");
}

void CheckPreparedGlyphs(RedXeCreateFn create, ProcessViewerGetTestDiagnosticsFn diagnostics,
                         decltype(&RedXeProcessViewerFailNextGlyphPreparation) failNext, Surface& surface)
{
    Viewer viewer(create, surface, "builtin.process-viewer", "process-viewer");
    Require(viewer.prepared->Prepare(nullptr) == E_INVALIDARG, "viewer accepted missing preparation context");
    Observation sample("process.activity", 7);
    sample.Integer(0, 17);
    sample.Text(1, L"Client");
    sample.Number(2, 10);
    sample.Integer(3, 1'000'000);
    viewer.Send(sample);
    Require(viewer.Draw(1) == S_OK, "prepared glyph control failed");
    const uint64_t control = surface.Pixels();
    const auto read = [&]()
    {
        ProcessViewerTestDiagnostics value{sizeof(value)};
        Require(diagnostics(&value) == S_OK, "prepared glyph diagnostics failed");
        return value;
    };
    const auto before = read();
    sample.Text(1, L"Caf\u00e9");
    for (uint32_t index = 0; index < 3; ++index)
        viewer.Send(sample);
    const auto delivered = read();
    Require(delivered.glyphUploadBytes == before.glyphUploadBytes && delivered.fontSessions == before.fontSessions,
            "worker delivery accessed the font or GPU atlas");
    Require(viewer.Draw(1, 0, 0, false) == S_OK && surface.Pixels() == control,
            "delivery changed the previously prepared frame");
    Require(viewer.Prepare() == S_OK, "new Unicode glyph preparation failed");
    const auto prepared = read();
    Require(prepared.glyphUploadCells == before.glyphUploadCells + 1 &&
                prepared.glyphUploadBytes == before.glyphUploadBytes + 48 * 48 &&
                prepared.fontSessions == before.fontSessions + 1,
            "one new glyph did not upload exactly one cell");
    Require(viewer.Draw(2, 0, 0, false) == S_OK && surface.Pixels() != control, "prepared Unicode label was not drawn");
    const uint64_t painted = surface.Pixels();
    sample.Text(1, L"M\u00fcnchen");
    viewer.Send(sample); // Arrival after preparation must wait for the next frame, including the raised view.
    Require(viewer.Draw(2, 1, 0, false) == S_OK && surface.Pixels() == painted,
            "tile and raised frame disagree after late delivery");
    Require(viewer.Prepare() == S_OK && viewer.Draw(3, 0, 0, false) == S_OK && surface.Pixels() != painted,
            "late Unicode label was lost on the next preparation");
    const auto next = read();
    Require(next.glyphUploadCells == prepared.glyphUploadCells + 1 &&
                next.glyphUploadBytes == prepared.glyphUploadBytes + 48 * 48,
            "late glyph preparation uploaded more than one cell");
    Require(viewer.Prepare() == S_FALSE && viewer.Draw(3, 1, 0, false) == S_OK,
            "clean preparation changed or prevented drawing");
    const auto clean = read();
    Require(clean.glyphUploadBytes == next.glyphUploadBytes && clean.fontSessions == next.fontSessions &&
                clean.preparations == next.preparations && clean.livePreparationFonts == 0,
            "clean preparation or Render did font/upload work");
    // Distinct strings share one temporary font session, which is released before either draw.
    sample.snapshot.rowCount = 2;
    sample.Text(1, L"B\u00f8");
    sample.Integer(0, 18, 1);
    sample.Text(1, L"\u00c5", 1);
    sample.Number(2, 5, 1);
    sample.Integer(3, 2'000'000, 1);
    viewer.Send(sample);
    Require(viewer.Prepare() == S_OK && viewer.Draw(4, 0, 0, false) == S_OK, "Unicode string batch failed");
    const auto batch = read();
    Require(batch.fontSessions == clean.fontSessions + 1 && batch.glyphUploadCells == clean.glyphUploadCells + 2 &&
                batch.glyphUploadBytes == clean.glyphUploadBytes + 2 * 48 * 48 && batch.livePreparationFonts == 0,
            "Unicode strings did not share and release one preparation font session");
    sample.Text(1, L"G\u03a9");
    viewer.Send(sample);
    failNext();
    Require(viewer.Prepare() == E_OUTOFMEMORY && viewer.Draw(5, 0, 0, false) == S_FALSE,
            "failed preparation still drew an unprepared sample");
    const auto failed = read();
    Require(failed.preparationFailures == batch.preparationFailures + 1 && failed.livePreparationFonts == 0 &&
                failed.glyphUploadBytes == batch.glyphUploadBytes,
            "failed preparation retained fonts or uploaded cells");
    Require(viewer.Prepare() == S_FALSE && read().preparationFailures == failed.preparationFailures,
            "unchanged failed preparation retried automatically");
    wil::com_ptr_nothrow<IRedXeInteractiveWidget> interactive;
    Require(viewer.widget.query_to(interactive.put()) == S_OK, "viewer input interface missing");
    const RedXePointerEvent pointer{sizeof(pointer), 1, RedXePointerKindMouse, RedXePointerPhaseDown, 20, 20};
    Require(interactive->OnPointer(&pointer) == S_FALSE, "failed preparation accepted input");
    const RedXeGpuPreparationContext retry{sizeof(retry), 96, Surface::width - 1, Surface::height, 0, 0, {}};
    Require(viewer.prepared->Prepare(&retry) == S_OK && viewer.Draw(6, 0, 0, false) == S_OK &&
                read().livePreparationFonts == 0,
            "extent change did not recover failed preparation");
    std::cout << "[       -- ] prepared_sample_bytes=" << clean.pendingSampleBytes
              << " glyph_upload_bytes_per_cell=" << 48 * 48 << '\n';
    Require(viewer.widget->SetVisible(FALSE) == S_OK && viewer.Prepare() == S_FALSE, "hidden preparation did work");
}

void CheckGlyphCellReuse(RedXeCreateFn create, Surface& surface)
{
    // U+FFFF has no font glyph. A fresh blank cell is the reference after a previously inked cell is reused.
    uint64_t emptyGlyph = 0;
    {
        Viewer viewer(create, surface, "builtin.process-viewer", "process-viewer");
        Observation sample("process.activity", 7);
        sample.Integer(0, 17);
        sample.Text(1, L"\uffff");
        sample.Number(2, 10);
        sample.Integer(3, 1'000'000);
        viewer.Send(sample);
        Require(viewer.Draw(1) == S_OK, "fresh unsupported glyph draw failed");
        emptyGlyph = surface.Pixels();
    }
    {
        Viewer viewer(create, surface, "builtin.process-viewer", "process-viewer");
        Observation sample("process.activity", 7);
        sample.Integer(0, 17);
        sample.Text(1, L"\u00e9");
        sample.Number(2, 10);
        sample.Integer(3, 1'000'000);
        viewer.Send(sample);
        Require(viewer.Draw(1) == S_OK && surface.Pixels() != emptyGlyph, "inked reuse control was empty");
        // The 21-by-21 atlas reserves 96 static cells. Fill the other 344 dynamic slots after the first accent.
        for (uint32_t start = 0; start < 344; start += 32)
        {
            std::array<wchar_t, 33> name{};
            const uint32_t count = std::min(32U, 344U - start);
            for (uint32_t index = 0; index < count; ++index)
                name[index] = static_cast<wchar_t>(0x0400U + start + index);
            sample.Text(1, name.data());
            viewer.Send(sample);
            Require(viewer.Prepare() == S_OK, "viewer capacity fill failed");
        }
        sample.Text(1, L"\uffff");
        viewer.Send(sample);
        Require(viewer.Draw(1, 0, 0) == S_OK && surface.Pixels() == emptyGlyph,
                "reused unsupported glyph retained the preceding glyph's ink");
    }
}

void CheckAtlasLifetime(RedXeCreateFn create, ProcessViewerGetTestDiagnosticsFn diagnostics,
                        decltype(&RedXeProcessViewerFailNextAtlasInitialization) failNext, Surface& surface)
{
    const auto check = [diagnostics](uint32_t bytes, uint32_t sets)
    {
        ProcessViewerTestDiagnostics info{sizeof(info)};
        Require(diagnostics(&info) == S_OK && info.cpuAtlasBytes == bytes && info.liveGpuResourceSets == sets,
                "viewer CPU atlas did not follow shared GPU-user lifetime");
    };
    check(0, 0);
    uint64_t pixels = 0;
    Observation sample("process.activity", 7);
    sample.Integer(0, 17);
    sample.Text(1, L"Caf\u00e9 - M\u00fcnchen");
    sample.Number(2, 10);
    sample.Integer(3, 1'000'000);
    {
        Viewer first(create, surface, "builtin.process-viewer", "process-viewer");
        check(1'048'576, 1);
        {
            Viewer second(create, surface, "builtin.memory-meter", "memory-meter");
            check(1'048'576, 1);
            first.Send(sample);
            Require(first.Draw(1) == S_OK && !first.Moving(), "viewer atlas pixel control failed");
            pixels = surface.Pixels();
            Require(first.widget->SetVisible(FALSE) == S_OK, "viewer atlas pre-loss hide failed");
            first.gpu->OnDeviceLost();
            first.gpu->OnDeviceLost(); // Idempotent release must not retire the second user's atlas.
            check(1'048'576, 1);
            Require(second.Draw(1) == S_OK, "surviving viewer lost its shared atlas");
            const RedXeGpuDeviceContext device{sizeof(device), surface.device.get(), DXGI_FORMAT_B8G8R8A8_UNORM,
                                               D3D_FEATURE_LEVEL_11_0};
            Require(first.gpu->OnDeviceCreated(&device) == S_OK && first.widget->SetVisible(TRUE) == S_OK &&
                        first.Draw(2) == S_OK && surface.Pixels() == pixels,
                    "shared viewer atlas recovery changed Unicode pixels");
            check(1'048'576, 1);
        }
        check(1'048'576, 1);
    }
    check(0, 0);
    {
        Viewer revisited(create, surface, "builtin.process-viewer", "process-viewer");
        revisited.Send(sample);
        Require(revisited.Draw(3) == S_OK && surface.Pixels() == pixels,
                "recreated viewer atlas changed Unicode pixels");
        check(1'048'576, 1);
    }
    check(0, 0);
    {
        Viewer cold(create, surface, "builtin.process-viewer", "process-viewer", nullptr, false);
        check(0, 0);
        Require(cold.gpu->OnDeviceCreated(nullptr) == E_INVALIDARG, "viewer accepted a missing device");
        const RedXeGpuDeviceContext device{sizeof(device), surface.device.get(), DXGI_FORMAT_B8G8R8A8_UNORM,
                                           D3D_FEATURE_LEVEL_11_0};
        failNext();
        Require(cold.gpu->OnDeviceCreated(&device) == E_OUTOFMEMORY, "viewer partial-init fault was not exercised");
        check(0, 0);
        Require(cold.gpu->OnDeviceCreated(&device) == S_OK && cold.widget->SetVisible(TRUE) == S_OK,
                "viewer could not retry after partial initialization failure");
        cold.Resize(Surface::width, Surface::height);
        cold.Send(sample);
        Require(cold.Draw(4) == S_OK && surface.Pixels() == pixels, "viewer partial-init retry changed Unicode pixels");
        check(1'048'576, 1);
    }
    check(0, 0);
}

void CheckRemainingViewers(RedXeCreateFn create, Surface& surface)
{
    struct Case final
    {
        const char* plugin;
        const char* type;
        const char* first;
        uint32_t firstColumns;
        const char* second;
        uint32_t secondColumns;
    };
    constexpr std::array cases{
        Case{"builtin.memory-meter", "memory-meter", "memory.summary", 12, nullptr, 0},
        Case{"builtin.network-meter", "network-meter", "network.interface", 22, "network.protocol", 6},
        Case{"builtin.storage-meter", "storage-meter", "storage.volume", 8, "storage.disk", 10},
        Case{"builtin.gpu-meter", "gpu-meter", "gpu.adapter", 15, nullptr, 0},
        Case{"builtin.gpu-processes", "gpu-processes", "gpu.process", 6, "process.activity", 7},
        Case{"builtin.power-meter", "power-meter", "power.summary", 5, "battery.list", 2},
        Case{"builtin.thermal-meter", "thermal-meter", "thermal.sensor", 4, "fan.sensor", 4},
    };
    for (size_t index = 0; index < cases.size(); ++index)
    {
        const Case& item = cases[index];
        Viewer viewer(create, surface, item.plugin, item.type);
        Observation first(item.first, item.firstColumns);
        Observation second(item.second, item.secondColumns, index == 5 ? 0u : 1u);
        switch (index)
        {
        case 0:
            first.Integer(0, 64'000'000'000);
            first.Integer(2, 20'000'000'000);
            first.Integer(3, 24'000'000'000);
            first.Integer(4, 64'000'000'000);
            first.Number(10, 0);
            break;
        case 1:
            first.Integer(0, 1);
            first.Text(1, L"Ethernet");
            first.Integer(4, 1);
            first.Integer(5, 1);
            first.Integer(7, 1'000'000'000);
            first.Integer(8, 1'000'000'000);
            first.Number(11, 1'000'000);
            first.Number(12, 2'000'000);
            first.Number(21, 5);
            second.Number(4, 0);
            break;
        case 2:
            first.Text(1, L"C:\\");
            first.Text(7, L"System");
            first.Integer(3, 1'000'000'000'000);
            first.Integer(4, 700'000'000'000);
            second.Number(5, 2);
            second.Number(8, 0);
            second.Number(9, 0);
            break;
        case 3:
            first.Integer(0, 1);
            first.Text(1, L"Adapter");
            first.Integer(6, 8'000'000'000);
            first.Number(14, 45);
            break;
        case 4:
            first.Integer(0, 100);
            first.Text(4, L"3D");
            first.Number(5, 20);
            second.Integer(0, 100);
            second.Text(1, L"Client");
            break;
        case 5:
            first.Integer(0, 1);
            first.Integer(1, 1);
            first.Number(4, 30);
            break;
        case 6:
            first.Text(0, L"cpu");
            first.Text(2, L"CPU sensor");
            first.Number(3, 40);
            second.Integer(2, 2000);
            second.Integer(3, 5000);
            break;
        default:
            break;
        }
        viewer.Send(first);
        if (item.second)
            viewer.Send(second);
        Require(viewer.Draw(1) == S_OK && !viewer.Moving(), "viewer family bootstrap did not park");
        for (uint32_t repeat = 0; repeat < 4; ++repeat)
        {
            viewer.Send(first);
            if (item.second)
                viewer.Send(second);
        }
        Require(viewer.Draw(2) == S_OK && !viewer.Moving(), "unchanged viewer family sample started motion");
        if (index == 0)
        {
            first.values[0][0].quality = RedXeDataQualityUnavailable;
            viewer.Send(first);
            Require(viewer.Draw(3) == S_OK && !viewer.Moving(), "unavailable memory observation started an empty ease");
            first.values[0][0].quality = RedXeDataQualityGood;
            viewer.Send(first);
            Require(viewer.Draw(4) == S_OK && viewer.Moving() && viewer.Draw(4.33f) == S_OK && !viewer.Moving(),
                    "restored memory capacity lost its visible ease");
        }
        if (index >= 1 && index <= 5)
        {
            const uint64_t initial = surface.Pixels();
            switch (index)
            {
            case 1:
                first.Number(11, 8'000'000);
                break;
            case 2:
                first.Integer(4, 450'000'000'000);
                second.Number(5, 20);
                break;
            case 3:
                first.Number(14, 60);
                break;
            case 4:
                first.Number(5, 45);
                break;
            case 5:
                first.Number(4, 60);
                break;
            default:
                break;
            }
            viewer.Send(first);
            if (item.second)
                viewer.Send(second);
            Require(viewer.Draw(3) == S_OK && viewer.Moving() && viewer.Draw(3.15f) == S_OK && viewer.Moving(),
                    "changed viewer-family value lost its visible ease");
            if (item.second)
                viewer.Send(second);
            Require(viewer.Draw(3.33f) == S_OK && !viewer.Moving() && surface.Pixels() != initial,
                    "viewer-family ease lost its target or restarted on an auxiliary delivery");
            if (index == 5)
            {
                first.Integer(1, 0);
                viewer.Send(first);
                Require(viewer.Draw(4) == S_OK && !viewer.Moving(), "absent battery retained an empty charge ease");
                first.Number(4, 40);
                viewer.Send(first);
                Require(viewer.Draw(5) == S_OK && !viewer.Moving(), "charge on an absent battery started motion");
            }
        }
        if (index == 6)
        {
            first.Number(3, 20);
            viewer.Send(first);
            Require(viewer.Draw(3) == S_OK && viewer.Moving() && viewer.Draw(3.33f) == S_OK && !viewer.Moving(),
                    "changed temperature lost its ease");
            first.Number(3, 24);
            viewer.Send(first);
            Require(viewer.Draw(4) == S_OK && viewer.Moving() && viewer.Draw(4.33f) == S_OK && !viewer.Moving(),
                    "large Celsius change on a saturated track lost its numeral ease");
            first.Number(3, 24.01);
            viewer.Send(first);
            Require(viewer.Draw(5) == S_OK && !viewer.Moving(), "tiny Celsius change started an empty ease");
        }
        Require(viewer.host.requests == 0, "viewer family added immediate animation frame requests");
    }
}
} // namespace

void TestProcessViewerScheduling(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] Process Viewer sample-driven wakes, visible easing and pixel stability\n";
    try
    {
        std::array<wchar_t, 32768> executable{};
        Require(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) != 0,
                "test executable path failed");
        const auto path = std::filesystem::path(executable.data()).parent_path() / L"Plugins" / L"ProcessViewer.dll";
        wil::unique_hmodule module(
            LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
        Require(module != nullptr, "viewer DLL load failed");
        const FARPROC address = GetProcAddress(module.get(), kRedXeCreateExport);
        RedXeCreateFn create = nullptr;
        static_assert(sizeof(address) == sizeof(create));
        std::memcpy(&create, &address, sizeof(create));
        Require(create != nullptr, "viewer factory export missing");
// Named export is bound to its declared Windows/plugin ABI; GetProcAddress erases that type.
// Owner: RedXe maintainers; review: 2027-01-31.
#pragma warning(suppress : 4191)
        const auto diagnostics = reinterpret_cast<ProcessViewerGetTestDiagnosticsFn>(
            GetProcAddress(module.get(), kProcessViewerGetTestDiagnosticsExport));
        Require(diagnostics != nullptr, "viewer atlas diagnostics missing");
// Named export is bound to its declared Windows/plugin ABI; GetProcAddress erases that type.
// Owner: RedXe maintainers; review: 2027-01-31.
#pragma warning(suppress : 4191)
        const auto failNext = reinterpret_cast<decltype(&RedXeProcessViewerFailNextAtlasInitialization)>(
            GetProcAddress(module.get(), "RedXeProcessViewerFailNextAtlasInitialization"));
        Require(failNext != nullptr, "viewer atlas fault export missing");
        Surface surface;
        if (!g_pixelReference.is_open())
        {
// Named export is bound to its declared Windows/plugin ABI; GetProcAddress erases that type.
// Owner: RedXe maintainers; review: 2027-01-31.
#pragma warning(suppress : 4191)
            const auto failGlyphs = reinterpret_cast<decltype(&RedXeProcessViewerFailNextGlyphPreparation)>(
                GetProcAddress(module.get(), "RedXeProcessViewerFailNextGlyphPreparation"));
            Require(failGlyphs != nullptr, "viewer glyph preparation fault export missing");
            CheckPreparedGlyphs(create, diagnostics, failGlyphs, surface);
            CheckAtlasLifetime(create, diagnostics, failNext, surface);
            CheckGlyphCellReuse(create, surface);
        }
        else
        {
            Viewer viewer(create, surface, "builtin.process-viewer", "process-viewer");
            Observation sample("process.activity", 7);
            sample.Integer(0, 17);
            sample.Text(1, L"Caf\u00e9 - M\u00fcnchen");
            sample.Number(2, 10);
            sample.Integer(3, 1'000'000);
            viewer.Send(sample);
            Require(viewer.Draw(1) == S_OK && viewer.Draw(1, 1) == S_OK, "Unicode reference draw failed");
        }
        CheckCadence(create, surface);
        CheckRanksAndAuxiliaryData(create, surface);
        CheckIdleProcessPulse(create, surface);
        CheckRemainingViewers(create, surface);
        CheckAllocationAndDeviceRecovery(create, surface);
    }
    catch (const std::exception& error)
    {
        success = false;
        std::cerr << "FAIL Process Viewer scheduling: " << error.what() << '\n';
    }
    catch (...)
    {
        success = false;
        std::wcerr << L"FAIL Process Viewer scheduling: unexpected exception\n";
    }
}

void TestProcessViewerPixelReference(const wchar_t* path, bool& success) noexcept
{
    try
    {
        g_pixelReference.open(std::filesystem::path(path), std::ios::trunc);
        Require(g_pixelReference.is_open(), "pixel reference file failed to open");
        const auto close = wil::scope_exit([]() noexcept { g_pixelReference.close(); });
        TestProcessViewerScheduling(success);
        Require(g_pixelReference.good(), "pixel reference file failed to write");
    }
    catch (...)
    {
        success = false;
        std::cerr << "FAIL Process Viewer pixel reference\n";
    }
}
