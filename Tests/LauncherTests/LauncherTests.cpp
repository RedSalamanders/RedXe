#include "../../Plugins/Launcher/LauncherPaging.h"
#include "../../Plugins/Launcher/LauncherTestContract.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Widget.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

#if defined(_DEBUG)
#include <crtdbg.h>
#endif

#include <d3d11.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.launcher";
constexpr char kWidgetTypeId[] = "launcher";
constexpr std::string_view kEmptyShortcuts = R"json({"shortcuts":[]})json";
constexpr HRESULT kTestFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
constexpr wchar_t kNotepad[] = L"C:\\Windows\\System32\\notepad.exe";

std::atomic<uint64_t> gRenderAllocations{0};
std::atomic<DWORD> gRenderThread{0};

#if defined(_DEBUG)
int __cdecl CountRenderAllocation(int allocationType, void*, size_t, int, long, const unsigned char*, int)
{
    if (allocationType != _HOOK_FREE && GetCurrentThreadId() == gRenderThread.load(std::memory_order_relaxed))
    {
        gRenderAllocations.fetch_add(1, std::memory_order_relaxed);
    }
    return TRUE;
}
#endif

template <typename Function> [[nodiscard]] Function Resolve(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] HRESULT BuildSiblingPath(const wchar_t* relativePath, std::array<wchar_t, 1024>& path) noexcept
{
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return HRESULT_FROM_WIN32(length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
    }
    wchar_t* separator = std::wcsrchr(path.data(), L'\\');
    if (!separator)
    {
        return E_UNEXPECTED;
    }
    ++separator;
    const size_t prefix = static_cast<size_t>(separator - path.data());
    const size_t suffix = std::wcslen(relativePath);
    if (prefix + suffix + 1 > path.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    std::memcpy(separator, relativePath, (suffix + 1) * sizeof(wchar_t));
    return S_OK;
}

class TestHost final : public IRedXeHost, public IRedXeSettingsQueue
{
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork*) noexcept override
    {
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ExecuteAction(const RedXeActionRequest*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ValidateAction(const RedXeActionRequest*,
                                             const RedXeActionDescriptor** descriptor) noexcept override
    {
        if (descriptor)
        {
            *descriptor = nullptr;
        }
        return S_OK;
    }

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IRedXeSettingsQueue) && supportsQueue)
            *result = static_cast<IRedXeSettingsQueue*>(this);
        else if (interfaceId == IID_IUnknown || interfaceId == __uuidof(IRedXeHost))
            *result = static_cast<IRedXeHost*>(this);
        else
        {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return 1;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        return 1;
    }

    HRESULT STDMETHODCALLTYPE GetDataProvider(const char*, IRedXeDataProvider** provider) noexcept override
    {
        if (provider)
        {
            *provider = nullptr;
        }
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override
    {
        ++frameRequests;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char*, const RedXeWidgetStatusReport*) noexcept override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char* instanceId, const char* json,
                                                    uint32_t bytes) noexcept override
    {
        persistCalls += 1;
        persistBytes = bytes;
        if (instanceId)
        {
            persistInstance.assign(instanceId);
        }
        if (json && bytes > 0)
        {
            persistJson.assign(json, bytes);
        }
        return persistResult;
    }

    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord*) noexcept override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueueWidgetSettings(const char* instanceId, const char* json,
                                                  uint32_t bytes) noexcept override
    {
        ++queueCalls;
        if (FAILED(queueResult))
            return queueResult;
        pendingInstance.assign(instanceId);
        pendingJson.assign(json, bytes);
        return S_OK;
    }

    void Drain() noexcept
    {
        if (!pendingJson.empty())
        {
            (void)PersistWidgetSettings(pendingInstance.c_str(), pendingJson.data(),
                                        static_cast<uint32_t>(pendingJson.size()));
            pendingJson.clear();
        }
    }

    bool supportsQueue = true;
    HRESULT queueResult = S_OK;
    uint32_t queueCalls = 0;
    std::string pendingInstance;
    std::string pendingJson;
    uint32_t frameRequests = 0;
    uint32_t persistCalls = 0;
    uint32_t persistBytes = 0;
    HRESULT persistResult = S_OK;
    std::string persistInstance;
    std::string persistJson;
};

struct RenderTarget final
{
    uint32_t width = 0;
    uint32_t height = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> view;
};

[[nodiscard]] HRESULT CreateRenderTarget(uint32_t width, uint32_t height, RenderTarget& target) noexcept
{
    constexpr std::array featureLevels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    RenderTarget created{};
    created.width = width;
    created.height = height;
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       featureLevels.data(), static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
                                       created.device.put(), &created.featureLevel, created.context.put());
    if (FAILED(result))
    {
        return result;
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    result = created.device->CreateTexture2D(&description, nullptr, created.texture.put());
    if (SUCCEEDED(result))
    {
        result = created.device->CreateRenderTargetView(created.texture.get(), nullptr, created.view.put());
    }
    if (FAILED(result))
    {
        return result;
    }
    target = std::move(created);
    return S_OK;
}

[[nodiscard]] HRESULT RenderFrame(IRedXeGpuWidget& widget, RenderTarget& target, float elapsedSeconds,
                                  float deltaSeconds, float originX = 0.0f, uint32_t width = 0,
                                  uint32_t height = 0) noexcept
{
    width = width == 0 ? target.width : width;
    height = height == 0 ? target.height : height;
    target.context->ClearState();
    ID3D11RenderTargetView* views[] = {target.view.get()};
    target.context->OMSetRenderTargets(1, views, nullptr);
    const D3D11_VIEWPORT viewport{
        originX, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f,
    };
    target.context->RSSetViewports(1, &viewport);
    const RedXeWidgetFrameContext widgetFrame{
        sizeof(RedXeWidgetFrameContext), width, height, USER_DEFAULT_SCREEN_DPI, elapsedSeconds, deltaSeconds,
    };
    const RedXeGpuFrameContext frame{sizeof(RedXeGpuFrameContext), &widgetFrame, target.context.get(), viewport};
    return widget.Render(&frame);
}

[[nodiscard]] HRESULT CheckWhiteIcon(RenderTarget& target, uint32_t centerX, uint32_t centerY) noexcept
{
    D3D11_TEXTURE2D_DESC description{};
    target.texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    HRESULT result = target.device->CreateTexture2D(&description, nullptr, staging.put());
    if (FAILED(result))
        return result;
    target.context->CopyResource(staging.get(), target.texture.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = target.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result))
        return result;
    const auto unmap = wil::scope_exit([&]() noexcept { target.context->Unmap(staging.get(), 0); });
    const auto* pixel = static_cast<const uint8_t*>(mapped.pData) + centerY * mapped.RowPitch + centerX * 4;
    if (pixel[0] < 240 || pixel[1] < 240 || pixel[2] < 240 || pixel[3] < 240)
    {
        std::wprintf(L"Launcher icon is missing at the actual viewport center.\n");
        return kTestFailure;
    }
    return S_OK;
}

[[nodiscard]] HRESULT CreateProvider(RedXeCreateFn create, std::string_view configuration, IRedXeHost* host,
                                     wil::com_ptr_nothrow<IRedXeWidgetProvider>& provider) noexcept
{
    try
    {
        std::string envelope;
        envelope.reserve(configuration.size() + 32);
        envelope.append("{\"plugin\":{},\"instance\":").append(configuration).append("}");
        RedXeFactoryOptions options{};
        options.sizeBytes = sizeof(options);
        options.configurationJsonUtf8 = envelope.data();
        options.configurationBytes = static_cast<uint32_t>(envelope.size());
        void* object = nullptr;
        const HRESULT result = create(__uuidof(IRedXeWidgetProvider), &options, host, kPluginId, &object);
        if (FAILED(result) || !object)
        {
            return FAILED(result) ? result : E_UNEXPECTED;
        }
        provider.attach(static_cast<IRedXeWidgetProvider*>(object));
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}

[[nodiscard]] HRESULT ExpectReject(RedXeCreateFn create, std::string_view configuration) noexcept
{
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    const HRESULT result = CreateProvider(create, configuration, nullptr, provider);
    return (FAILED(result) && !provider) ? S_OK : kTestFailure;
}

[[nodiscard]] std::string MakeShortcutListJson(uint32_t count, const char* iconSize = nullptr)
{
    std::string json = "{";
    if (iconSize && iconSize[0] != '\0')
    {
        json += "\"iconSize\":\"";
        json += iconSize;
        json += "\",";
    }
    json += "\"shortcuts\":[";
    for (uint32_t index = 0; index < count; ++index)
    {
        if (index != 0)
        {
            json += ',';
        }
        char item[80]{};
        (void)sprintf_s(item, R"({"target":"C:\\Windows\\System32\\n%02u.exe"})", index);
        json += item;
    }
    json += "]}";
    return json;
}

[[nodiscard]] HRESULT WritePng(const wchar_t* path) noexcept
{
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result =
        CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    constexpr UINT edge = 32;
    std::array<uint8_t, edge * edge * 4> pixels{};
    for (uint8_t& value : pixels)
    {
        value = 255;
    }
    wil::com_ptr_nothrow<IWICBitmap> bitmap;
    result = factory->CreateBitmapFromMemory(edge, edge, GUID_WICPixelFormat32bppPBGRA, edge * 4,
                                             static_cast<UINT>(pixels.size()), pixels.data(), bitmap.put());
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICStream> stream;
    result = factory->CreateStream(stream.put());
    if (FAILED(result))
    {
        return result;
    }
    result = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
    result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put());
    if (FAILED(result))
    {
        return result;
    }
    result = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICBitmapFrameEncode> frame;
    result = encoder->CreateNewFrame(frame.put(), nullptr);
    if (FAILED(result))
    {
        return result;
    }
    result = frame->Initialize(nullptr);
    if (FAILED(result))
    {
        return result;
    }
    result = frame->SetSize(edge, edge);
    if (FAILED(result))
    {
        return result;
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    result = frame->SetPixelFormat(&format);
    if (FAILED(result))
    {
        return result;
    }
    result = frame->WriteSource(bitmap.get(), nullptr);
    if (FAILED(result))
    {
        return result;
    }
    result = frame->Commit();
    if (FAILED(result))
    {
        return result;
    }
    return encoder->Commit();
}

[[nodiscard]] HRESULT WriteShortcut(const wchar_t* path) noexcept
{
    wil::com_ptr_nothrow<IShellLinkW> link;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put()));
    if (FAILED(result))
    {
        return result;
    }
    result = link->SetPath(kNotepad);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IPersistFile> persist;
    result = link.query_to(persist.put());
    if (FAILED(result))
    {
        return result;
    }
    return persist->Save(path, TRUE);
}

[[nodiscard]] HRESULT AttachGpu(IRedXeWidget& widget, RenderTarget& target, IRedXeGpuWidget** gpuOut) noexcept
{
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpu;
    HRESULT result = widget.QueryInterface(__uuidof(IRedXeGpuWidget), reinterpret_cast<void**>(gpu.put()));
    if (FAILED(result) || !gpu)
    {
        return FAILED(result) ? result : kTestFailure;
    }
    const RedXeGpuDeviceContext deviceContext{sizeof(RedXeGpuDeviceContext), target.device.get(),
                                              DXGI_FORMAT_B8G8R8A8_UNORM, target.featureLevel};
    result = gpu->OnDeviceCreated(&deviceContext);
    if (FAILED(result))
    {
        return result;
    }
    const RedXeGpuTargetSizeContext sizeContext{sizeof(RedXeGpuTargetSizeContext), target.width, target.height,
                                                USER_DEFAULT_SCREEN_DPI};
    result = gpu->OnTargetSizeChanged(&sizeContext);
    if (FAILED(result))
    {
        return result;
    }
    *gpuOut = gpu.detach();
    return S_OK;
}

[[nodiscard]] HRESULT ValidateFactoryAndSettings(RedXeCreateFn create, RedXeEnumeratePluginsFn enumerate,
                                                 RedXeGetPluginSettingsContractFn getContract) noexcept
{
    if (enumerate(nullptr, nullptr) != E_POINTER || getContract(kPluginId, nullptr) != E_POINTER)
    {
        return kTestFailure;
    }
    const RedXePluginMetadata* metadata = nullptr;
    uint32_t count = 0;
    HRESULT result = enumerate(&metadata, &count);
    if (FAILED(result) || count != 1 || !metadata || !metadata->id || std::strcmp(metadata->id, kPluginId) != 0)
    {
        return kTestFailure;
    }
    const RedXePluginSettingsContract* contract = nullptr;
    result = getContract(kPluginId, &contract);
    if (FAILED(result) || !contract || contract->sizeBytes != sizeof(RedXePluginSettingsContract))
    {
        return kTestFailure;
    }
    // An unsatisfied launch target is a warning tile, not a rejection, and unknown names are the host's call
    // (SettingsTests); a non-launch action without an icon and the former iconPng member are structural rejections.
    if (FAILED(ExpectReject(create, R"json({"shortcuts":[{"target":""}]})json")) ||
        FAILED(ExpectReject(create, R"json({"shortcuts":[{"action":"page.next"}]})json")) ||
        FAILED(ExpectReject(create, R"json({"shortcuts":[{"target":"C:\\x.exe","iconPng":"C:\\i.png"}]})json")) ||
        FAILED(ExpectReject(create, R"json({"extra":1,"shortcuts":[]})json")) ||
        FAILED(ExpectReject(create, R"json({"shortcuts":[{"target":"C:\\Windows\\notepad.exe","nope":1}]})json")) ||
        FAILED(ExpectReject(create, MakeShortcutListJson(kLauncherMaximumShortcuts + 1))) ||
        FAILED(ExpectReject(create, R"json({"iconSize":"jumbo"})json")) ||
        FAILED(ExpectReject(create, R"json({"iconSize":"auto"})json")))
    {
        std::wprintf(L"Launcher factory rejected a valid document or accepted an invalid one.\n");
        return kTestFailure;
    }
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    const std::string thirtyTwo = MakeShortcutListJson(kLauncherMaximumShortcuts);
    if (FAILED(CreateProvider(create, thirtyTwo, nullptr, provider)))
    {
        std::wprintf(L"Launcher factory rejected 32 shortcuts.\n");
        return kTestFailure;
    }
    if (FAILED(CreateProvider(create, R"json({"iconSize":"small"})json", nullptr, provider)) ||
        FAILED(CreateProvider(create, R"json({"iconSize":"medium"})json", nullptr, provider)) ||
        FAILED(CreateProvider(create, R"json({"iconSize":"large"})json", nullptr, provider)))
    {
        std::wprintf(L"Launcher factory rejected a valid iconSize.\n");
        return kTestFailure;
    }
    result = CreateProvider(create, kEmptyShortcuts, nullptr, provider);
    if (FAILED(result) || !provider)
    {
        return FAILED(result) ? result : kTestFailure;
    }
    void* window = reinterpret_cast<void*>(1);
    wil::com_ptr_nothrow<IRedXeWidget> widget;
    result = provider->CreateWidget(kWidgetTypeId, "launcher.test", widget.put());
    if (FAILED(result) || !widget)
    {
        return FAILED(result) ? result : kTestFailure;
    }
    if (widget->QueryInterface(__uuidof(IRedXeWindowWidget), &window) != E_NOINTERFACE || window)
    {
        return kTestFailure;
    }
    wil::com_ptr_nothrow<IRedXeInteractiveWidget> interactive;
    wil::com_ptr_nothrow<IRedXeRaisedWidget> raised;
    if (FAILED(widget.query_to(interactive.put())) || FAILED(widget.query_to(raised.put())) || !interactive || !raised)
    {
        return kTestFailure;
    }
    RedXeRaisedExtent extent = static_cast<RedXeRaisedExtent>(0);
    if (raised->GetRaisedExtent(&extent) != S_OK || extent != RedXeRaisedExtentHalf)
    {
        return kTestFailure;
    }
    uint32_t written = 1;
    std::array<char, 8> buffer{};
    if (widget->CollectPersistentSettings(buffer.data(), static_cast<uint32_t>(buffer.size()), &written) != S_FALSE ||
        written != 0)
    {
        return kTestFailure;
    }
    return S_OK;
}

[[nodiscard]] HRESULT ValidatePinsAndRendering(RedXeCreateFn create, LauncherSetTestPinDirectoryFn setPinDirectory,
                                               LauncherGetTestDiagnosticsFn getDiagnostics,
                                               LauncherResetTestDiagnosticsFn resetDiagnostics) noexcept
{
    resetDiagnostics();
    if (FAILED(setPinDirectory(L"")))
    {
        return kTestFailure;
    }
    TestHost host;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> emptyProvider;
    HRESULT result = CreateProvider(create, kEmptyShortcuts, &host, emptyProvider);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeWidget> emptyWidget;
    result = emptyProvider->CreateWidget(kWidgetTypeId, "launcher.empty", emptyWidget.put());
    if (FAILED(result))
    {
        return result;
    }
    RenderTarget target{};
    result = CreateRenderTarget(480, 480, target);
    if (FAILED(result))
    {
        return result;
    }
    IRedXeGpuWidget* rawGpu = nullptr;
    result = AttachGpu(*emptyWidget, target, &rawGpu);
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpu;
    gpu.attach(rawGpu);
    if (FAILED(result))
    {
        return result;
    }
    result = emptyWidget->SetVisible(TRUE);
    if (FAILED(result))
    {
        return result;
    }
    LauncherTestDiagnostics emptyDiagnostics{sizeof(LauncherTestDiagnostics)};
    result = getDiagnostics(&emptyDiagnostics);
    if (FAILED(result) || emptyDiagnostics.displayCount != 0 || emptyDiagnostics.usingTaskbarPins != 0)
    {
        std::wprintf(L"Automated empty list still showed pins.\n");
        return kTestFailure;
    }
    result = RenderFrame(*gpu, target, 0.0f, 1.0f / 60.0f);
    if (FAILED(result))
    {
        return result;
    }
    result = getDiagnostics(&emptyDiagnostics);
    if (FAILED(result) || emptyDiagnostics.lastDrawCount != 1)
    {
        std::wprintf(L"Empty tile did not draw the hint background only.\n");
        return kTestFailure;
    }

    std::array<wchar_t, MAX_PATH> tempRoot{};
    if (GetTempPathW(static_cast<DWORD>(tempRoot.size()), tempRoot.data()) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    std::array<wchar_t, MAX_PATH> pinDir{};
    if (swprintf_s(pinDir.data(), pinDir.size(), L"%sRedXeLauncherPins-%lu", tempRoot.data(), GetCurrentProcessId()) <=
        0)
    {
        return E_UNEXPECTED;
    }
    CreateDirectoryW(pinDir.data(), nullptr);
    std::array<wchar_t, MAX_PATH> lnkPath{};
    (void)swprintf_s(lnkPath.data(), lnkPath.size(), L"%s\\Alpha.lnk", pinDir.data());
    result = WriteShortcut(lnkPath.data());
    if (FAILED(result))
    {
        return result;
    }
    result = setPinDirectory(pinDir.data());
    if (FAILED(result))
    {
        return result;
    }
    const uint64_t extractsBefore = emptyDiagnostics.extractCalls;
    result = emptyWidget->SetVisible(TRUE);
    if (FAILED(result))
    {
        return result;
    }
    LauncherTestDiagnostics pinDiagnostics{sizeof(LauncherTestDiagnostics)};
    result = getDiagnostics(&pinDiagnostics);
    if (FAILED(result) || pinDiagnostics.displayCount == 0 || pinDiagnostics.usingTaskbarPins == 0 ||
        pinDiagnostics.authoredCount != pinDiagnostics.displayCount || host.queueCalls != 1 || host.persistCalls != 0 ||
        host.pendingJson.find("Alpha.lnk") == std::string::npos)
    {
        std::wprintf(L"Injected pin directory did not populate the empty list.\n");
        return kTestFailure;
    }
    result = RenderFrame(*gpu, target, 0.1f, 1.0f / 60.0f);
    if (FAILED(result))
    {
        return result;
    }
    result = getDiagnostics(&pinDiagnostics);
    if (FAILED(result) || pinDiagnostics.lastDrawCount != 2 || pinDiagnostics.lastInstanceCount == 0)
    {
        std::wprintf(L"Pin grid did not issue background plus instanced icon draws.\n");
        return kTestFailure;
    }
    const uint64_t extractsAfterPins = pinDiagnostics.extractCalls;
    if (extractsAfterPins <= extractsBefore)
    {
        std::wprintf(L"Pin import did not extract icons.\n");
        return kTestFailure;
    }
    gpu->OnDeviceLost();
    const RedXeGpuDeviceContext recreate{sizeof(RedXeGpuDeviceContext), target.device.get(), DXGI_FORMAT_B8G8R8A8_UNORM,
                                         target.featureLevel};
    result = gpu->OnDeviceCreated(&recreate);
    if (FAILED(result))
    {
        return result;
    }
    result = getDiagnostics(&pinDiagnostics);
    if (FAILED(result) || pinDiagnostics.extractCalls != extractsAfterPins)
    {
        std::wprintf(L"Device loss re-extracted shell icons.\n");
        return kTestFailure;
    }
    result = RenderFrame(*gpu, target, 0.2f, 1.0f / 60.0f, -40.0f);
    if (FAILED(result))
    {
        return result;
    }

    uint32_t written = 1;
    std::array<char, 4097> collect{};
    if (emptyWidget->CollectPersistentSettings(collect.data(), static_cast<uint32_t>(collect.size()), &written) !=
            S_OK ||
        written == 0 || std::string_view(collect.data(), written) != host.pendingJson)
    {
        std::wprintf(L"Imported pins were not retained for collect fallback.\n");
        return kTestFailure;
    }
    if (emptyWidget->CollectPersistentSettings(collect.data(), 2, &written) !=
            HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) ||
        written != 0)
    {
        std::wprintf(L"Collect did not report a bounded persist buffer.\n");
        return kTestFailure;
    }
    if (FAILED(emptyWidget->SetVisible(FALSE)) || FAILED(emptyWidget->SetVisible(TRUE)) || host.queueCalls != 1)
    {
        std::wprintf(L"Hide/show re-queued imported pins.\n");
        return kTestFailure;
    }
    host.persistResult = E_ACCESSDENIED;
    host.Drain();
    wil::com_ptr_nothrow<IRedXeInteractiveWidget> importedInteractive;
    if (FAILED(emptyWidget.query_to(importedInteractive.put())))
        return kTestFailure;
    const RedXeDropItem failedItem{sizeof(RedXeDropItem), 0, L"https://example.com/rejected"};
    const RedXeDropEvent failedDrop{sizeof(RedXeDropEvent), 1, 1, 1, &failedItem};
    if (SUCCEEDED(importedInteractive->OnDrop(&failedDrop)) ||
        emptyWidget->CollectPersistentSettings(collect.data(), static_cast<uint32_t>(collect.size()), &written) !=
            S_OK ||
        std::string_view(collect.data(), written).find("Alpha.lnk") == std::string_view::npos ||
        std::string_view(collect.data(), written).find("rejected") != std::string_view::npos)
    {
        std::wprintf(L"Failed drop mutated imported pins.\n");
        return kTestFailure;
    }
    host.persistResult = S_OK;

    // Saved imports remain editable configured shortcuts, even when the taskbar directory is unavailable later.
    TestHost savedHost;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> savedProvider;
    wil::com_ptr_nothrow<IRedXeWidget> savedWidget;
    if (FAILED(setPinDirectory(L"")) ||
        FAILED(CreateProvider(create, std::string_view(collect.data(), written), &savedHost, savedProvider)) ||
        FAILED(savedProvider->CreateWidget(kWidgetTypeId, "launcher.saved", savedWidget.put())) ||
        FAILED(savedWidget->SetVisible(TRUE)) || FAILED(getDiagnostics(&pinDiagnostics)) ||
        pinDiagnostics.authoredCount != 1 || pinDiagnostics.displayCount != 1 || savedHost.queueCalls != 0)
    {
        std::wprintf(L"Saved pin import did not reload as authored shortcuts.\n");
        return kTestFailure;
    }
    (void)setPinDirectory(pinDir.data());
    for (const bool available : {false, true})
    {
        TestHost fallbackHost;
        fallbackHost.supportsQueue = available;
        fallbackHost.queueResult = HRESULT_FROM_WIN32(ERROR_BUSY);
        wil::com_ptr_nothrow<IRedXeWidgetProvider> fallbackProvider;
        wil::com_ptr_nothrow<IRedXeWidget> fallbackWidget;
        if (FAILED(CreateProvider(create, kEmptyShortcuts, &fallbackHost, fallbackProvider)) ||
            FAILED(fallbackProvider->CreateWidget(kWidgetTypeId, "launcher.fallback", fallbackWidget.put())) ||
            FAILED(fallbackWidget->SetVisible(TRUE)) ||
            fallbackWidget->CollectPersistentSettings(collect.data(), static_cast<uint32_t>(collect.size()),
                                                      &written) != S_OK ||
            written == 0 || fallbackHost.persistCalls != 0)
        {
            std::wprintf(L"Pin import fallback did not keep a collectable persist payload.\n");
            return kTestFailure;
        }
    }

    {
        const std::wstring longDirectory = std::wstring(pinDir.data()) + L"\\Long";
        if (!CreateDirectoryW(longDirectory.c_str(), nullptr))
            return HRESULT_FROM_WIN32(GetLastError());
        std::array<std::wstring, 8> paths;
        const auto cleanLongPins = wil::scope_exit(
            [&]() noexcept
            {
                (void)setPinDirectory(pinDir.data());
                for (const auto& path : paths)
                    if (!path.empty())
                        (void)DeleteFileW(path.c_str());
                (void)RemoveDirectoryW(longDirectory.c_str());
            });
        const int directoryBytes =
            WideCharToMultiByte(CP_UTF8, 0, longDirectory.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (directoryBytes <= 0 || directoryBytes >= 200)
            return kTestFailure;
        const size_t characters = (511U - static_cast<size_t>(directoryBytes) - 5U) / 3U;
        for (size_t index = 0; index < paths.size(); ++index)
        {
            paths[index] = longDirectory + L"\\" + static_cast<wchar_t>(L'A' + index) +
                           std::wstring(characters, L'\x96ea') + L".lnk";
            if (FAILED(WriteShortcut(paths[index].c_str())))
                return kTestFailure;
        }
        TestHost boundedHost;
        wil::com_ptr_nothrow<IRedXeWidgetProvider> boundedProvider;
        wil::com_ptr_nothrow<IRedXeWidget> boundedWidget;
        if (FAILED(setPinDirectory(longDirectory.c_str())) ||
            FAILED(CreateProvider(create, kEmptyShortcuts, &boundedHost, boundedProvider)) ||
            FAILED(boundedProvider->CreateWidget(kWidgetTypeId, "launcher.bounded", boundedWidget.put())) ||
            FAILED(boundedWidget->SetVisible(TRUE)) || FAILED(getDiagnostics(&pinDiagnostics)) ||
            pinDiagnostics.authoredCount == 0 || pinDiagnostics.authoredCount >= 8 ||
            pinDiagnostics.displayCount != pinDiagnostics.authoredCount || boundedHost.pendingJson.empty() ||
            boundedHost.pendingJson.size() > 4096)
        {
            std::wprintf(L"Long pin paths were not bounded for persist.\n");
            return kTestFailure;
        }
        wil::com_ptr_nothrow<IRedXeWidgetProvider> roundTrip;
        if (FAILED(CreateProvider(create, boundedHost.pendingJson, &boundedHost, roundTrip)))
        {
            std::wprintf(L"Bounded persist JSON was not a valid factory instance.\n");
            return kTestFailure;
        }
    }

    std::wprintf(L"Pin import checks passed; starting PNG layout checks.\n");
    std::array<wchar_t, MAX_PATH> pngPath{};
    (void)swprintf_s(pngPath.data(), pngPath.size(), L"%s\\override.png", pinDir.data());
    result = WritePng(pngPath.data());
    if (FAILED(result))
    {
        return result;
    }
    char pngUtf8[MAX_PATH]{};
    if (WideCharToMultiByte(CP_UTF8, 0, pngPath.data(), -1, pngUtf8, static_cast<int>(sizeof(pngUtf8)), nullptr,
                            nullptr) <= 0)
    {
        return E_FAIL;
    }
    std::string escaped = R"json({"shortcuts":[{"target":"C:\\Windows\\System32\\notepad.exe","icon":"png:)json";
    for (const char* cursor = pngUtf8; *cursor != '\0'; ++cursor)
    {
        if (*cursor == '\\' || *cursor == '"')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(*cursor);
    }
    escaped.append(R"json("}]})json");
    wil::com_ptr_nothrow<IRedXeWidgetProvider> pngProvider;
    result = CreateProvider(create, escaped, &host, pngProvider);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeWidget> pngWidget;
    result = pngProvider->CreateWidget(kWidgetTypeId, "launcher.png", pngWidget.put());
    if (FAILED(result))
    {
        return result;
    }
    IRedXeGpuWidget* pngGpuRaw = nullptr;
    result = AttachGpu(*pngWidget, target, &pngGpuRaw);
    wil::com_ptr_nothrow<IRedXeGpuWidget> pngGpu;
    pngGpu.attach(pngGpuRaw);
    if (FAILED(result))
    {
        return result;
    }
    result = pngWidget->SetVisible(TRUE);
    if (FAILED(result))
    {
        return result;
    }
    result = RenderFrame(*pngGpu, target, 0.0f, 1.0f / 60.0f);
    if (FAILED(result))
    {
        return result;
    }
    LauncherTestDiagnostics pngDiagnostics{sizeof(LauncherTestDiagnostics)};
    result = getDiagnostics(&pngDiagnostics);
    if (FAILED(result) || pngDiagnostics.authoredCount != 1 || pngDiagnostics.usingTaskbarPins != 0)
    {
        std::wprintf(L"PNG override shortcut did not stay authored.\n");
        return kTestFailure;
    }

    wil::com_ptr_nothrow<IRedXeInteractiveWidget> layoutInput;
    if (FAILED(pngWidget.query_to(layoutInput.put())))
        return kTestFailure;
    // One largest-size notification, followed by the tile and overlay draws on the same device.
    for (uint32_t edge : {120U, 480U, 120U, 480U})
    {
        result = RenderFrame(*pngGpu, target, 0.0f, 0.0f, 0.0f, edge, edge);
        if (SUCCEEDED(result))
            result = CheckWhiteIcon(target, edge / 2, edge / 2);
        const float center = static_cast<float>(edge) * 0.5f;
        const RedXePointerEvent hit{sizeof(hit), 1, RedXePointerKindMouse, RedXePointerPhaseDown, center, center};
        const RedXePointerEvent cancel{sizeof(cancel),          1,      RedXePointerKindMouse,
                                       RedXePointerPhaseCancel, center, center};
        const RedXePointerEvent padding{sizeof(padding), 1, RedXePointerKindMouse, RedXePointerPhaseDown, 1.0f, 1.0f};
        if (FAILED(result) || layoutInput->OnPointer(&hit) != S_OK || layoutInput->OnPointer(&cancel) != S_FALSE ||
            layoutInput->OnPointer(&padding) != S_FALSE)
        {
            std::wprintf(L"PNG icon hit-test failed at %u DIP.\n", edge);
            return kTestFailure;
        }
    }

    gRenderThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    gRenderAllocations.store(0, std::memory_order_relaxed);
#if defined(_DEBUG)
    _CrtSetAllocHook(CountRenderAllocation);
#endif
    for (uint32_t edge : {120U, 480U, 120U, 480U})
    {
        result = RenderFrame(*pngGpu, target, 0.5f, 1.0f / 60.0f, 0.0f, edge, edge);
        if (FAILED(result))
            break;
    }
#if defined(_DEBUG)
    _CrtSetAllocHook(nullptr);
#endif
    if (FAILED(result) || gRenderAllocations.load(std::memory_order_relaxed) != 0)
    {
        std::wprintf(L"Launcher Render allocated on the steady path.\n");
        return kTestFailure;
    }

    wil::com_ptr_nothrow<IRedXeInteractiveWidget> interactive;
    if (FAILED(pngWidget.query_to(interactive.put())) || !interactive)
    {
        return kTestFailure;
    }
    const RedXePointerEvent down{sizeof(RedXePointerEvent), 1,      RedXePointerKindMouse,
                                 RedXePointerPhaseDown,     240.0f, 240.0f};
    const RedXePointerEvent up{sizeof(RedXePointerEvent), 1,      RedXePointerKindMouse,
                               RedXePointerPhaseUp,       240.0f, 240.0f};
    if (interactive->OnPointer(&down) != S_OK || interactive->OnPointer(&up) != S_OK)
    {
        return kTestFailure;
    }
    LauncherTestDiagnostics launchDiagnostics{sizeof(LauncherTestDiagnostics)};
    result = getDiagnostics(&launchDiagnostics);
    if (FAILED(result) || launchDiagnostics.launchCount == 0 || launchDiagnostics.shellExecuteCount != 0 ||
        launchDiagnostics.lastLaunchKind != 1 || launchDiagnostics.lastVerbWasNull == 0 ||
        launchDiagnostics.lastShellMask != SEE_MASK_FLAG_NO_UI)
    {
        std::wprintf(L"Automated launch did not count a filesystem ShellExecuteExW-shaped call.\n");
        return kTestFailure;
    }
    result = RenderFrame(*pngGpu, target, 0.51f, 1.0f / 60.0f);
    if (FAILED(result) || host.frameRequests == 0)
    {
        std::wprintf(L"Launch animation did not request a follow-up frame.\n");
        return kTestFailure;
    }

    const wchar_t* dropTarget = kNotepad;
    RedXeDropItem dropItem{sizeof(RedXeDropItem), 0, dropTarget};
    const RedXeDropEvent dropEvent{sizeof(RedXeDropEvent), 12.0f, 12.0f, 1, &dropItem};
    result = interactive->OnDrop(&dropEvent);
    if (result != S_FALSE)
    {
        std::wprintf(L"Duplicate drop was not ignored.\n");
        return kTestFailure;
    }

    TestHost dropHost;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> dropProvider;
    result = CreateProvider(create, kEmptyShortcuts, &dropHost, dropProvider);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeWidget> dropWidget;
    result = dropProvider->CreateWidget(kWidgetTypeId, "launcher.drop", dropWidget.put());
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeInteractiveWidget> dropInteractive;
    if (FAILED(dropWidget.query_to(dropInteractive.put())))
    {
        return kTestFailure;
    }
    constexpr wchar_t kUrl[] = L"https://example.com/path";
    const std::array items{
        RedXeDropItem{sizeof(RedXeDropItem), 0, kNotepad},
        RedXeDropItem{sizeof(RedXeDropItem), 0, kUrl},
    };
    const RedXeDropEvent multi{sizeof(RedXeDropEvent), 8.0f, 8.0f, static_cast<uint32_t>(items.size()), items.data()};
    if (FAILED(dropWidget->SetVisible(TRUE)) || dropHost.queueCalls != 1)
        return kTestFailure;
    dropHost.Drain();
    result = dropInteractive->OnDrop(&multi);
    if (result != S_OK || dropHost.persistCalls != 2 || dropHost.persistJson.find("notepad.exe") == std::string::npos ||
        dropHost.persistJson.find("https://example.com/path") == std::string::npos ||
        dropHost.persistJson.find("Alpha.lnk") == std::string::npos)
    {
        std::wprintf(L"Drop persist did not retain imported pins alongside filesystem and URL targets.\n");
        return kTestFailure;
    }
    uint32_t dropWritten = 1;
    if (dropWidget->CollectPersistentSettings(collect.data(), static_cast<uint32_t>(collect.size()), &dropWritten) !=
            S_FALSE ||
        dropWritten != 0)
    {
        return kTestFailure;
    }

    std::array<wchar_t, 64> extraPath{};
    LauncherTestDiagnostics dropDiagnostics{sizeof(LauncherTestDiagnostics)};
    result = getDiagnostics(&dropDiagnostics);
    if (FAILED(result))
    {
        return result;
    }
    uint32_t extraIndex = 0;
    while (dropDiagnostics.authoredCount < kLauncherMaximumShortcuts)
    {
        (void)swprintf_s(extraPath.data(), extraPath.size(), L"C:\\Windows\\System32\\extra%u.exe", extraIndex);
        const RedXeDropItem extraItem{sizeof(RedXeDropItem), 0, extraPath.data()};
        const RedXeDropEvent extraEvent{sizeof(RedXeDropEvent), 1.0f, 1.0f, 1, &extraItem};
        if (dropInteractive->OnDrop(&extraEvent) != S_OK)
        {
            std::wprintf(L"Launcher rejected a shortcut before reaching 32.\n");
            return kTestFailure;
        }
        ++extraIndex;
        result = getDiagnostics(&dropDiagnostics);
        if (FAILED(result) || extraIndex > kLauncherMaximumShortcuts + 4)
        {
            std::wprintf(L"Launcher drop fill did not reach 32 shortcuts.\n");
            return FAILED(result) ? result : kTestFailure;
        }
    }
    const RedXeDropItem overflowItem{sizeof(RedXeDropItem), 0, L"C:\\Windows\\System32\\extra33.exe"};
    const RedXeDropEvent overflowEvent{sizeof(RedXeDropEvent), 1.0f, 1.0f, 1, &overflowItem};
    if (dropInteractive->OnDrop(&overflowEvent) != S_FALSE)
    {
        std::wprintf(L"A 33rd shortcut was accepted.\n");
        return kTestFailure;
    }
    result = getDiagnostics(&dropDiagnostics);
    if (FAILED(result) || dropDiagnostics.authoredCount != kLauncherMaximumShortcuts)
    {
        std::wprintf(L"Launcher drop cap did not stay at 32 shortcuts.\n");
        return FAILED(result) ? result : kTestFailure;
    }

    (void)setPinDirectory(nullptr);
    DeleteFileW(lnkPath.data());
    DeleteFileW(pngPath.data());
    RemoveDirectoryW(pinDir.data());
    return S_OK;
}

[[nodiscard]] bool LauncherCellsAreRegular(const std::array<std::array<float, 4>, kLauncherMaximumShortcuts>& cells,
                                           uint32_t packed, uint32_t columns, float width, float contentHeight) noexcept
{
    if (packed == 0 || columns == 0)
    {
        return false;
    }
    constexpr float kEps = 0.75f;
    const float half = cells[0][2];
    if (half < 4.0f || std::fabs(cells[0][3] - half) > kEps)
    {
        return false;
    }
    for (uint32_t index = 0; index < packed; ++index)
    {
        if (std::fabs(cells[index][2] - half) > kEps || std::fabs(cells[index][3] - half) > kEps)
        {
            return false;
        }
        if (cells[index][0] - half < -kEps || cells[index][0] + half > width + kEps || cells[index][1] - half < -kEps ||
            cells[index][1] + half > contentHeight + kEps)
        {
            return false;
        }
        if (index > 0 && index % columns != 0 &&
            std::fabs((cells[index][0] - cells[index - 1][0]) - (cells[1][0] - cells[0][0])) > kEps)
        {
            return false;
        }
        if (index >= columns &&
            std::fabs((cells[index][1] - cells[index - columns][1]) - (cells[columns][1] - cells[0][1])) > kEps)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool LauncherCellsKeepMinimumGutters(
    const std::array<std::array<float, 4>, kLauncherMaximumShortcuts>& cells, uint32_t packed, uint32_t columns,
    float width, float contentHeight, UINT dpi) noexcept
{
    if (packed == 0 || columns == 0)
    {
        return false;
    }
    constexpr float kEps = 0.75f;
    const float minGutter = LauncherDipToPixels(kLauncherMinGutterDip, dpi);
    const float edgeInset = LauncherDipToPixels(kLauncherEdgeInsetDip, dpi);
    const float iconEdge = cells[0][2] * 2.0f;
    for (uint32_t index = 0; index < packed; ++index)
    {
        const float half = cells[index][2];
        if (cells[index][0] - half + kEps < edgeInset || cells[index][0] + half > width - edgeInset + kEps ||
            cells[index][1] - half + kEps < edgeInset || cells[index][1] + half > contentHeight - edgeInset + kEps)
        {
            return false;
        }
        if (index > 0 && index % columns != 0 && cells[index][0] - cells[index - 1][0] + kEps < iconEdge + minGutter)
        {
            return false;
        }
        if (index >= columns && cells[index][1] - cells[index - columns][1] + kEps < iconEdge + minGutter)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] HRESULT ExpectNamedSizeLayout(uint32_t width, uint32_t height, UINT dpi, uint32_t shortcutCount,
                                            LauncherIconSize iconSize, const LauncherPageGeometry& huge) noexcept
{
    const auto pages = ComputeLauncherPages(width, height, dpi, shortcutCount, 0, iconSize);
    if (pages.columns == 0 || pages.rows == 0 || pages.iconSizePx <= 0.0f || pages.cellSizePx <= 0.0f)
    {
        std::wprintf(L"Launcher %hs paging did not publish a regular cell.\n", LauncherIconSizeName(iconSize));
        return kTestFailure;
    }
    const bool paging = pages.pageCount > 1;
    if (paging != (pages.indicatorHeightPx > 0.5f) ||
        (!paging && pages.contentHeightPx + 0.5f < static_cast<float>(height)))
    {
        std::wprintf(L"Launcher %hs reserved the page-dot strip incorrectly.\n", LauncherIconSizeName(iconSize));
        return kTestFailure;
    }
    if (pages.rows <= huge.rows && pages.iconSizePx + 0.5f >= huge.iconSizePx)
    {
        std::wprintf(L"Launcher %hs on a tall tile should produce more rows or a smaller icon than huge.\n",
                     LauncherIconSizeName(iconSize));
        return kTestFailure;
    }
    std::array<std::array<float, 4>, kLauncherMaximumShortcuts> cells{};
    FillLauncherPageCells(width, height, dpi, shortcutCount, pages, cells);
    const uint32_t packed = LauncherPackedIconCount(pages, shortcutCount);
    if (packed == 0 || packed > shortcutCount ||
        !LauncherCellsAreRegular(cells, packed, pages.columns, static_cast<float>(width), pages.contentHeightPx) ||
        !LauncherCellsKeepMinimumGutters(cells, packed, pages.columns, static_cast<float>(width), pages.contentHeightPx,
                                         dpi))
    {
        std::wprintf(L"Launcher %hs icons were clipped or used uneven cell geometry.\n",
                     LauncherIconSizeName(iconSize));
        return kTestFailure;
    }
    const float expectedHalf = LauncherSpreadIconPixels(static_cast<float>(width), pages.contentHeightPx, pages.columns,
                                                        pages.rows, pages.iconSizePx, dpi) *
                               0.5f;
    if (std::fabs(cells[0][2] - expectedHalf) > 0.75f)
    {
        std::wprintf(L"Launcher %hs draw size did not follow the named DIP cell.\n", LauncherIconSizeName(iconSize));
        return kTestFailure;
    }
    return S_OK;
}

[[nodiscard]] HRESULT ValidatePages(RedXeCreateFn create, LauncherGetTestDiagnosticsFn getDiagnostics) noexcept
{
    const auto hugeLarge = ComputeLauncherPages(480, 480, 96, 8, 0, LauncherIconSize::Huge);
    if (hugeLarge.pageCount < 2 || hugeLarge.visibleCount == 0 || hugeLarge.visibleCount >= 8)
    {
        std::wprintf(L"Huge icons on a large tile should paginate eight shortcuts.\n");
        return kTestFailure;
    }
    const auto automaticLarge = ComputeLauncherPages(480, 480, 96, 8, 0, LauncherIconSize::Automatic);
    if (automaticLarge.pageCount != 1 || automaticLarge.visibleCount != 8)
    {
        std::wprintf(L"Automatic icons on a large tile should fit eight shortcuts without dots.\n");
        return kTestFailure;
    }
    const auto smallLarge = ComputeLauncherPages(480, 480, 96, 8, 0, LauncherIconSize::Small);
    const auto mediumLarge = ComputeLauncherPages(480, 480, 96, 8, 0, LauncherIconSize::Medium);
    const auto largeLarge = ComputeLauncherPages(480, 480, 96, 8, 0, LauncherIconSize::Large);
    if (smallLarge.pageCount != 1 || mediumLarge.pageCount != 1 || largeLarge.pageCount != 1 ||
        smallLarge.visibleCount != 8 || mediumLarge.visibleCount != 8 || largeLarge.visibleCount != 8 ||
        smallLarge.pageCount == hugeLarge.pageCount || mediumLarge.visibleCount == hugeLarge.visibleCount ||
        largeLarge.visibleCount == hugeLarge.visibleCount)
    {
        std::wprintf(L"Named icon sizes should change per-page count versus huge on a large tile.\n");
        return kTestFailure;
    }
    const auto paged = ComputeLauncherPages(160, 160, 96, 8, 0, LauncherIconSize::Huge);
    if (paged.pageCount < 2 || paged.visibleCount == 0 || paged.visibleCount >= 8)
    {
        std::wprintf(L"A compact launcher tile should page overflow shortcuts.\n");
        return kTestFailure;
    }
    const auto automaticCompact = ComputeLauncherPages(160, 160, 96, 8, 0, LauncherIconSize::Automatic);
    if (automaticCompact.pageCount < 2)
    {
        std::wprintf(L"Automatic icons should still paginate when the 72 DIP floor cannot hold eight shortcuts.\n");
        return kTestFailure;
    }
    if (HitLauncherPageDot(80.0f, 150.0f, 160, 160, 96, paged.pageCount) == UINT32_MAX)
    {
        std::wprintf(L"Paged launcher dots should be hittable at the bottom strip.\n");
        return kTestFailure;
    }

    constexpr uint32_t kTallWidth = 256;
    constexpr uint32_t kTallHeight = 720;
    const auto hugeTall = ComputeLauncherPages(kTallWidth, kTallHeight, 96, 8, 0, LauncherIconSize::Huge);
    if (hugeTall.pageCount < 2 || hugeTall.iconSizePx < 180.0f || hugeTall.columns == 0 || hugeTall.rows == 0)
    {
        std::wprintf(L"Huge icons on a tall 256x720 tile should stay jumbo-class and paginate eight shortcuts.\n");
        return kTestFailure;
    }
    std::array<std::array<float, 4>, kLauncherMaximumShortcuts> hugeCells{};
    FillLauncherPageCells(kTallWidth, kTallHeight, 96, 8, hugeTall, hugeCells);
    const uint32_t hugePacked = LauncherPackedIconCount(hugeTall, 8);
    if (!LauncherCellsAreRegular(hugeCells, hugePacked, hugeTall.columns, static_cast<float>(kTallWidth),
                                 hugeTall.contentHeightPx) ||
        !LauncherCellsKeepMinimumGutters(hugeCells, hugePacked, hugeTall.columns, static_cast<float>(kTallWidth),
                                         hugeTall.contentHeightPx, 96))
    {
        std::wprintf(L"Huge launcher icons were clipped on a tall tile.\n");
        return kTestFailure;
    }
    const auto smallTall = ComputeLauncherPages(kTallWidth, kTallHeight, 96, 8, 0, LauncherIconSize::Small);
    std::array<std::array<float, 4>, kLauncherMaximumShortcuts> smallCells{};
    FillLauncherPageCells(kTallWidth, kTallHeight, 96, 8, smallTall, smallCells);
    const uint32_t smallPacked = LauncherPackedIconCount(smallTall, 8);
    if (smallTall.pageCount != 1 || smallTall.columns > 2 || smallTall.rows < 4 || smallPacked != 8 ||
        smallCells[smallPacked - 1][1] - smallCells[0][1] < smallTall.contentHeightPx * 0.45f ||
        !LauncherCellsKeepMinimumGutters(smallCells, smallPacked, smallTall.columns, static_cast<float>(kTallWidth),
                                         smallTall.contentHeightPx, 96))
    {
        std::wprintf(L"Small icons on a tall 256x720 tile clustered instead of spreading with gutters.\n");
        return kTestFailure;
    }
    const auto thirtyTwoSmall =
        ComputeLauncherPages(720, 720, 96, kLauncherMaximumShortcuts, 0, LauncherIconSize::Small);
    if (thirtyTwoSmall.pageCount != 1 || thirtyTwoSmall.visibleCount != kLauncherMaximumShortcuts)
    {
        std::wprintf(L"Small icons should fit 32 shortcuts on a large square tile.\n");
        return kTestFailure;
    }
    std::array<std::array<float, 4>, kLauncherMaximumShortcuts> thirtyTwoCells{};
    FillLauncherPageCells(720, 720, 96, kLauncherMaximumShortcuts, thirtyTwoSmall, thirtyTwoCells);
    if (!LauncherCellsAreRegular(thirtyTwoCells, kLauncherMaximumShortcuts, thirtyTwoSmall.columns, 720.0f,
                                 thirtyTwoSmall.contentHeightPx) ||
        !LauncherCellsKeepMinimumGutters(thirtyTwoCells, kLauncherMaximumShortcuts, thirtyTwoSmall.columns, 720.0f,
                                         thirtyTwoSmall.contentHeightPx, 96))
    {
        std::wprintf(L"32 small launcher icons did not keep even gutters.\n");
        return kTestFailure;
    }
    if (FAILED(ExpectNamedSizeLayout(kTallWidth, kTallHeight, 96, 8, LauncherIconSize::Small, hugeTall)) ||
        FAILED(ExpectNamedSizeLayout(kTallWidth, kTallHeight, 96, 8, LauncherIconSize::Medium, hugeTall)) ||
        FAILED(ExpectNamedSizeLayout(kTallWidth, kTallHeight, 96, 8, LauncherIconSize::Large, hugeTall)) ||
        FAILED(ExpectNamedSizeLayout(kTallWidth, kTallHeight, 96, 8, LauncherIconSize::Automatic, hugeTall)))
    {
        return kTestFailure;
    }

    constexpr std::string_view eight =
        R"json({"shortcuts":[{"target":"C:\\Windows\\System32\\notepad.exe"},{"target":"C:\\Windows\\System32\\cmd.exe"},{"target":"C:\\Windows\\System32\\write.exe"},{"target":"C:\\Windows\\System32\\winver.exe"},{"target":"C:\\Windows\\explorer.exe"},{"target":"C:\\Windows\\System32\\mspaint.exe"},{"target":"C:\\Windows\\System32\\control.exe"},{"target":"C:\\Windows\\System32\\calc.exe"}]})json";
    constexpr std::string_view smallEight =
        R"json({"iconSize":"small","shortcuts":[{"target":"C:\\Windows\\System32\\notepad.exe"},{"target":"C:\\Windows\\System32\\cmd.exe"},{"target":"C:\\Windows\\System32\\write.exe"},{"target":"C:\\Windows\\System32\\winver.exe"},{"target":"C:\\Windows\\explorer.exe"},{"target":"C:\\Windows\\System32\\mspaint.exe"},{"target":"C:\\Windows\\System32\\control.exe"},{"target":"C:\\Windows\\System32\\calc.exe"}]})json";
    {
        TestHost tallHost;
        wil::com_ptr_nothrow<IRedXeWidgetProvider> smallProvider;
        HRESULT result = CreateProvider(create, smallEight, &tallHost, smallProvider);
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<IRedXeWidget> smallWidget;
        result = smallProvider->CreateWidget(kWidgetTypeId, "launcher.tall.small", smallWidget.put());
        if (FAILED(result))
        {
            return result;
        }
        RenderTarget tall{};
        result = CreateRenderTarget(256, 720, tall);
        if (FAILED(result))
        {
            return result;
        }
        IRedXeGpuWidget* smallGpuRaw = nullptr;
        result = AttachGpu(*smallWidget, tall, &smallGpuRaw);
        wil::com_ptr_nothrow<IRedXeGpuWidget> smallGpu;
        smallGpu.attach(smallGpuRaw);
        if (FAILED(result))
        {
            return result;
        }
        result = smallWidget->SetVisible(TRUE);
        if (FAILED(result) || FAILED(RenderFrame(*smallGpu, tall, 0.0f, 0.0f, 0.0f, 256, 720)))
        {
            return FAILED(result) ? result : kTestFailure;
        }
        LauncherTestDiagnostics smallDiag{sizeof(LauncherTestDiagnostics)};
        result = getDiagnostics(&smallDiag);
        if (FAILED(result) || smallDiag.pageCount != 1 || smallDiag.columns > 2 || smallDiag.rows < 4 ||
            smallDiag.iconHalfExtentPx == 0 || smallDiag.iconHalfExtentPx >= 64 ||
            (smallDiag.rows <= hugeTall.rows &&
             smallDiag.iconHalfExtentPx >= static_cast<uint32_t>(hugeTall.iconSizePx * 0.5f + 0.5f)))
        {
            std::wprintf(L"Small icons on a tall tile did not draw a spread smaller grid.\n");
            return kTestFailure;
        }

        wil::com_ptr_nothrow<IRedXeWidgetProvider> hugeProvider;
        result = CreateProvider(create, eight, &tallHost, hugeProvider);
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<IRedXeWidget> hugeWidget;
        result = hugeProvider->CreateWidget(kWidgetTypeId, "launcher.tall.huge", hugeWidget.put());
        if (FAILED(result))
        {
            return result;
        }
        IRedXeGpuWidget* hugeGpuRaw = nullptr;
        result = AttachGpu(*hugeWidget, tall, &hugeGpuRaw);
        wil::com_ptr_nothrow<IRedXeGpuWidget> hugeGpu;
        hugeGpu.attach(hugeGpuRaw);
        if (FAILED(result))
        {
            return result;
        }
        result = hugeWidget->SetVisible(TRUE);
        if (FAILED(result) || FAILED(RenderFrame(*hugeGpu, tall, 0.0f, 0.0f, 0.0f, 256, 720)))
        {
            return FAILED(result) ? result : kTestFailure;
        }
        LauncherTestDiagnostics hugeDiag{sizeof(LauncherTestDiagnostics)};
        result = getDiagnostics(&hugeDiag);
        if (FAILED(result) || hugeDiag.pageCount < 2 || hugeDiag.iconHalfExtentPx <= smallDiag.iconHalfExtentPx)
        {
            std::wprintf(L"Huge icons on a tall tile did not keep a larger draw size than small.\n");
            return kTestFailure;
        }
    }

    TestHost host;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    HRESULT result = CreateProvider(create, eight, &host, provider);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeWidget> widget;
    result = provider->CreateWidget(kWidgetTypeId, "launcher.pages", widget.put());
    if (FAILED(result))
    {
        return result;
    }
    RenderTarget target{};
    result = CreateRenderTarget(160, 160, target);
    if (FAILED(result))
    {
        return result;
    }
    IRedXeGpuWidget* gpuRaw = nullptr;
    result = AttachGpu(*widget, target, &gpuRaw);
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpu;
    gpu.attach(gpuRaw);
    if (FAILED(result))
    {
        return result;
    }
    result = widget->SetVisible(TRUE);
    if (FAILED(result))
    {
        return result;
    }
    result = RenderFrame(*gpu, target, 0.0f, 0.0f, 0.0f, 160, 160);
    if (FAILED(result))
    {
        return result;
    }
    LauncherTestDiagnostics diagnostics{sizeof(LauncherTestDiagnostics)};
    result = getDiagnostics(&diagnostics);
    if (FAILED(result) || diagnostics.pageCount < 2 || diagnostics.pageIndex != 0)
    {
        std::wprintf(L"Compact eight-shortcut launcher did not report multiple pages.\n");
        return kTestFailure;
    }
    const uint64_t launchesBefore = diagnostics.launchCount;
    wil::com_ptr_nothrow<IRedXeInteractiveWidget> interactive;
    if (FAILED(widget.query_to(interactive.put())) || !interactive)
    {
        return kTestFailure;
    }
    const RedXePointerEvent down{sizeof(RedXePointerEvent), 2,      RedXePointerKindTouch,
                                 RedXePointerPhaseDown,     140.0f, 20.0f};
    const RedXePointerEvent move{sizeof(RedXePointerEvent), 2,     RedXePointerKindTouch,
                                 RedXePointerPhaseMove,     20.0f, 20.0f};
    const RedXePointerEvent up{sizeof(RedXePointerEvent), 2, RedXePointerKindTouch, RedXePointerPhaseUp, 20.0f, 20.0f};
    const HRESULT downResult = interactive->OnPointer(&down);
    if (FAILED(downResult) || interactive->OnPointer(&move) != S_OK)
    {
        std::wprintf(L"Launcher did not consume a one-finger page swipe.\n");
        return kTestFailure;
    }
    result = getDiagnostics(&diagnostics);
    if (FAILED(result) || diagnostics.pageIndex != 0 || diagnostics.pageSlidePx == 0)
    {
        std::wprintf(L"Launcher page swipe did not follow the finger before release.\n");
        return kTestFailure;
    }
    if (interactive->OnPointer(&up) != S_OK)
    {
        std::wprintf(L"Launcher did not consume a one-finger page swipe release.\n");
        return kTestFailure;
    }
    result = getDiagnostics(&diagnostics);
    if (FAILED(result) || diagnostics.pageIndex == 0)
    {
        std::wprintf(L"Launcher page swipe did not advance the page.\n");
        return kTestFailure;
    }
    if (diagnostics.pageSlidePx == 0 && diagnostics.pageSettling == 0)
    {
        std::wprintf(L"Launcher page swipe committed without a settle offset.\n");
        return kTestFailure;
    }
    Sleep(300);
    result = RenderFrame(*gpu, target, 0.6f, 1.0f / 60.0f, 0.0f, 160, 160);
    result = getDiagnostics(&diagnostics);
    if (FAILED(result) || diagnostics.pageIndex == 0 || diagnostics.pageSlidePx != 0 || diagnostics.pageSettling != 0)
    {
        std::wprintf(L"Launcher page swipe did not settle the icon grid.\n");
        return kTestFailure;
    }
    const uint32_t committedPage = diagnostics.pageIndex;
    const uint32_t targetPage = committedPage == 0 ? 1U : 0U;
    const float gap = LauncherDipToPixels(kLauncherPageIndicatorDotGapDip, 96);
    const float total = gap * static_cast<float>(diagnostics.pageCount - 1);
    const float dotX = static_cast<float>(160) * 0.5f - total * 0.5f + gap * static_cast<float>(targetPage);
    const RedXePointerEvent dotDown{sizeof(RedXePointerEvent), 3,    RedXePointerKindTouch,
                                    RedXePointerPhaseDown,     dotX, 150.0f};
    const RedXePointerEvent dotUp{sizeof(RedXePointerEvent), 3,    RedXePointerKindTouch,
                                  RedXePointerPhaseUp,       dotX, 150.0f};
    if (HitLauncherPageDot(dotX, 150.0f, 160, 160, 96, diagnostics.pageCount) != targetPage ||
        interactive->OnPointer(&dotDown) != S_OK || interactive->OnPointer(&dotUp) != S_OK)
    {
        std::wprintf(L"Launcher did not consume a page-dot tap.\n");
        return kTestFailure;
    }
    result = getDiagnostics(&diagnostics);
    if (FAILED(result) || diagnostics.pageIndex != targetPage ||
        (diagnostics.pageSlidePx == 0 && diagnostics.pageSettling == 0))
    {
        std::wprintf(L"Launcher page-dot tap did not animate to the selected page.\n");
        return kTestFailure;
    }
    if (diagnostics.launchCount != launchesBefore)
    {
        std::wprintf(L"Launcher page swipe launched a shortcut.\n");
        return kTestFailure;
    }
    return S_OK;
}

[[nodiscard]] HRESULT ValidateGrid(RedXeCreateFn create, LauncherGetTestDiagnosticsFn getDiagnostics) noexcept
{
    constexpr std::string_view four =
        R"json({"shortcuts":[{"target":"C:\\Windows\\System32\\notepad.exe"},{"target":"C:\\Windows\\System32\\cmd.exe"},{"target":"C:\\Windows\\System32\\write.exe"},{"target":"C:\\Windows\\System32\\winver.exe"}]})json";
    TestHost host;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    HRESULT result = CreateProvider(create, four, &host, provider);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeWidget> widget;
    result = provider->CreateWidget(kWidgetTypeId, "launcher.grid", widget.put());
    if (FAILED(result))
    {
        return result;
    }
    RenderTarget wide{};
    result = CreateRenderTarget(960, 160, wide);
    if (FAILED(result))
    {
        return result;
    }
    IRedXeGpuWidget* gpuRaw = nullptr;
    result = AttachGpu(*widget, wide, &gpuRaw);
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpu;
    gpu.attach(gpuRaw);
    if (FAILED(result))
    {
        return result;
    }
    result = widget->SetVisible(TRUE);
    if (FAILED(result))
    {
        return result;
    }
    LauncherTestDiagnostics diagnostics{sizeof(LauncherTestDiagnostics)};
    result = getDiagnostics(&diagnostics);
    if (FAILED(result) || diagnostics.displayCount != 4 || diagnostics.columns < 4 || diagnostics.rows != 1)
    {
        std::wprintf(L"Wide strip did not prefer extra columns.\n");
        return kTestFailure;
    }
    RenderTarget square{};
    result = CreateRenderTarget(160, 160, square);
    if (FAILED(result))
    {
        return result;
    }
    const RedXeGpuDeviceContext deviceContext{sizeof(RedXeGpuDeviceContext), square.device.get(),
                                              DXGI_FORMAT_B8G8R8A8_UNORM, square.featureLevel};
    gpu->OnDeviceLost();
    result = gpu->OnDeviceCreated(&deviceContext);
    if (FAILED(result))
    {
        return result;
    }
    const RedXeGpuTargetSizeContext sizeContext{sizeof(RedXeGpuTargetSizeContext), 160, 160, USER_DEFAULT_SCREEN_DPI};
    result = gpu->OnTargetSizeChanged(&sizeContext);
    if (FAILED(result))
    {
        return result;
    }
    result = getDiagnostics(&diagnostics);
    if (FAILED(result) || diagnostics.columns == 0 || diagnostics.rows == 0)
    {
        return kTestFailure;
    }
    return S_OK;
}

[[nodiscard]] HRESULT Run() noexcept
{
    const bool compilerLoaded = GetModuleHandleW(L"d3dcompiler_47.dll") != nullptr;
    (void)SetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", L"1");
    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole))
    {
        return ole;
    }
    std::array<wchar_t, 1024> path{};
    HRESULT result = BuildSiblingPath(L"Plugins\\Launcher.dll", path);
    if (FAILED(result))
    {
        OleUninitialize();
        return result;
    }
    wil::unique_hmodule module{
        LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (!module)
    {
        OleUninitialize();
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const RedXeCreateFn create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const RedXeEnumeratePluginsFn enumerate =
        Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const RedXeGetPluginSettingsContractFn getContract =
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);
    const RedXePluginShutdownFn shutdown = Resolve<RedXePluginShutdownFn>(module.get(), kRedXePluginShutdownExport);
    const LauncherSetTestPinDirectoryFn setPin =
        Resolve<LauncherSetTestPinDirectoryFn>(module.get(), kLauncherSetTestPinDirectoryExport);
    const LauncherGetTestDiagnosticsFn getDiagnostics =
        Resolve<LauncherGetTestDiagnosticsFn>(module.get(), kLauncherGetTestDiagnosticsExport);
    const LauncherResetTestDiagnosticsFn resetDiagnostics =
        Resolve<LauncherResetTestDiagnosticsFn>(module.get(), kLauncherResetTestDiagnosticsExport);
    if (!create || !enumerate || !getContract || !shutdown || !setPin || !getDiagnostics || !resetDiagnostics)
    {
        OleUninitialize();
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    result = ValidateFactoryAndSettings(create, enumerate, getContract);
    if (FAILED(result))
    {
        std::wprintf(L"ValidateFactoryAndSettings failed: 0x%08X\n", static_cast<unsigned int>(result));
    }
    if (SUCCEEDED(result))
    {
        result = ValidatePinsAndRendering(create, setPin, getDiagnostics, resetDiagnostics);
        if (FAILED(result))
            std::wprintf(L"ValidatePinsAndRendering failed: 0x%08X\n", static_cast<unsigned int>(result));
    }
    if (SUCCEEDED(result))
    {
        result = ValidateGrid(create, getDiagnostics);
        if (FAILED(result))
            std::wprintf(L"ValidateGrid failed: 0x%08X\n", static_cast<unsigned int>(result));
    }
    if (SUCCEEDED(result))
    {
        result = ValidatePages(create, getDiagnostics);
        if (FAILED(result))
            std::wprintf(L"ValidatePages failed: 0x%08X\n", static_cast<unsigned int>(result));
    }
    shutdown();
    OleUninitialize();
    if (FAILED(result))
    {
        return result;
    }
    if (!compilerLoaded && GetModuleHandleW(L"d3dcompiler_47.dll"))
    {
        std::wprintf(L"Launcher loaded d3dcompiler_47.dll.\n");
        return kTestFailure;
    }
    return S_OK;
}
} // namespace

int wmain() noexcept
{
    const HRESULT result = Run();
    if (FAILED(result))
    {
        std::wprintf(L"Launcher tests failed: 0x%08X\n", static_cast<unsigned int>(result));
        return 1;
    }
    std::wprintf(L"Launcher factory, pin fallback, WARP, launch, and drop tests passed.\n");
    return 0;
}
