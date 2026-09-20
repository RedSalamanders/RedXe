#include "WindowCapture.h"

// The C++/WinRT projection and the capture interop headers are external implementation: their templates emit
// aggregate and deleted-special-member warnings under Level4 that are not ours to fix.
#pragma warning(push, 0)
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

#include <exception>
#include <limits>
#include <new>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwmapi.lib")

// The projection's factory and delegate templates trip the same warnings inside our own function bodies.
#pragma warning(push)
#pragma warning(disable : 5246 4265 4625 4626 5026 5027 4324)

namespace RedXe
{
namespace
{
constexpr DWORD kFrameTimeoutMilliseconds = 5000;

[[nodiscard]] HRESULT WritePng(const wchar_t* path, const BYTE* pixels, UINT width, UINT height, UINT stride) noexcept
{
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICStream> stream;
    result = factory->CreateStream(stream.put());
    if (SUCCEEDED(result))
    {
        result = stream->InitializeFromFilename(path, GENERIC_WRITE);
    }
    wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(result))
    {
        result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put());
    }
    if (SUCCEEDED(result))
    {
        result = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
    }
    wil::com_ptr_nothrow<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(result))
    {
        result = encoder->CreateNewFrame(frame.put(), nullptr);
    }
    if (SUCCEEDED(result))
    {
        result = frame->Initialize(nullptr);
    }
    if (SUCCEEDED(result))
    {
        result = frame->SetSize(width, height);
    }
    if (SUCCEEDED(result))
    {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        result = frame->SetPixelFormat(&format);
        if (SUCCEEDED(result) && format != GUID_WICPixelFormat32bppBGRA)
        {
            result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        }
    }
    if (SUCCEEDED(result))
    {
        // The last row of a cropped region ends before its full stride; size the buffer to what is really there.
        const UINT bytes = stride * (height - 1) + width * 4U;
        result = frame->WritePixels(height, stride, bytes, const_cast<BYTE*>(pixels));
    }
    if (SUCCEEDED(result))
    {
        result = frame->Commit();
    }
    if (SUCCEEDED(result))
    {
        result = encoder->Commit();
    }
    return result;
}

// The hardware Direct3D 11 device and the capture item for one window: what both the screenshot and the support
// probe need before a frame can exist. Throws winrt::hresult_error like the projection it wraps.
struct CaptureSource
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice projected{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
};

[[nodiscard]] CaptureSource OpenCaptureSource(HWND window)
{
    namespace Capture = winrt::Windows::Graphics::Capture;
    namespace Direct3D = winrt::Windows::Graphics::DirectX::Direct3D11;
    CaptureSource source;
    winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                           nullptr, 0, D3D11_SDK_VERSION, source.device.put(), nullptr,
                                           source.context.put()));
    wil::com_ptr_nothrow<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(source.device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())));
    wil::com_ptr_nothrow<IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    winrt::check_hresult(inspectable->QueryInterface(winrt::guid_of<Direct3D::IDirect3DDevice>(),
                                                     winrt::put_abi(source.projected)));
    const auto interop = winrt::get_activation_factory<Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    winrt::check_hresult(
        interop->CreateForWindow(window, winrt::guid_of<Capture::GraphicsCaptureItem>(), winrt::put_abi(source.item)));
    return source;
}
} // namespace

HRESULT QueryWindowCaptureSupport(HWND window, SIZE* surfaceSize) noexcept
{
    if (!window || !surfaceSize)
    {
        return E_POINTER;
    }
    *surfaceSize = SIZE{};
    try
    {
        if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported())
        {
            return E_NOTIMPL;
        }
        const CaptureSource source = OpenCaptureSource(window);
        const auto size = source.item.Size();
        *surfaceSize = SIZE{size.Width, size.Height};
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const winrt::hresult_error& error)
    {
        return error.code();
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT SaveWindowScreenshot(HWND window, const wchar_t* pngPath, const RECT* clientCrop) noexcept
{
    if (!window || !pngPath || pngPath[0] == L'\0')
    {
        return E_POINTER;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId() || !IsWindowVisible(window) || IsIconic(window))
    {
        return E_INVALIDARG;
    }
    // The captured frame covers the window's extended frame bounds (what DWM shows), so a client-space crop is
    // offset by the client origin inside those bounds.
    RECT crop{};
    bool cropping = false;
    if (clientCrop)
    {
        RECT client{};
        RECT frame{};
        POINT origin{};
        if (!GetClientRect(window, &client) || !ClientToScreen(window, &origin) ||
            FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        RECT clipped{};
        if (!IntersectRect(&clipped, clientCrop, &client))
        {
            return E_INVALIDARG;
        }
        crop = RECT{clipped.left + origin.x - frame.left, clipped.top + origin.y - frame.top,
                    clipped.right + origin.x - frame.left, clipped.bottom + origin.y - frame.top};
        cropping = true;
    }
    // C++/WinRT reports failures as exceptions; this boundary turns them back into an HRESULT and never
    // substitutes a desktop image or a partial layer for the requested window.
    try
    {
        namespace Capture = winrt::Windows::Graphics::Capture;
        if (!Capture::GraphicsCaptureSession::IsSupported())
        {
            return E_NOTIMPL;
        }
        const CaptureSource source = OpenCaptureSource(window);
        const auto& device = source.device;
        const auto& context = source.context;
        const auto& item = source.item;
        wil::unique_event_nothrow frameArrived;
        winrt::check_hresult(frameArrived.create(wil::EventOptions::ManualReset));
        auto pool = Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            source.projected, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
            item.Size());
        auto session = pool.CreateCaptureSession(item);
        const auto close = wil::scope_exit(
            [&]() noexcept
            {
                session.Close();
                pool.Close();
            });
        const auto revoke = pool.FrameArrived(
            winrt::auto_revoke, [event = frameArrived.get()](const auto&, const auto&) noexcept { SetEvent(event); });
        session.IsCursorCaptureEnabled(false);
        session.StartCapture();
        if (WaitForSingleObject(frameArrived.get(), kFrameTimeoutMilliseconds) != WAIT_OBJECT_0)
        {
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        auto frame = pool.TryGetNextFrame();
        if (!frame)
        {
            return E_UNEXPECTED;
        }
        const auto closeFrame = wil::scope_exit([&]() noexcept { frame.Close(); });
        const auto access =
            frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        wil::com_ptr_nothrow<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(texture.put())));
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.MiscFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        wil::com_ptr_nothrow<ID3D11Texture2D> staging;
        winrt::check_hresult(device->CreateTexture2D(&description, nullptr, staging.put()));
        context->CopyResource(staging.get(), texture.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        winrt::check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
        const auto unmap = wil::scope_exit([&]() noexcept { context->Unmap(staging.get(), 0); });
        const auto size = frame.ContentSize();
        if (size.Width <= 0 || size.Height <= 0 || static_cast<UINT>(size.Width) > description.Width ||
            static_cast<UINT>(size.Height) > description.Height ||
            mapped.RowPitch > std::numeric_limits<UINT>::max() / static_cast<UINT>(size.Height))
        {
            return E_UNEXPECTED;
        }
        if (!cropping)
        {
            return WritePng(pngPath, static_cast<const BYTE*>(mapped.pData), static_cast<UINT>(size.Width),
                            static_cast<UINT>(size.Height), mapped.RowPitch);
        }
        const RECT content{0, 0, size.Width, size.Height};
        RECT region{};
        if (!IntersectRect(&region, &crop, &content))
        {
            return E_INVALIDARG;
        }
        const BYTE* first = static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(region.top) * mapped.RowPitch +
                            static_cast<size_t>(region.left) * 4U;
        return WritePng(pngPath, first, static_cast<UINT>(region.right - region.left),
                        static_cast<UINT>(region.bottom - region.top), mapped.RowPitch);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const winrt::hresult_error& error)
    {
        return error.code();
    }
    catch (...)
    {
        return E_FAIL;
    }
}
} // namespace RedXe

#pragma warning(pop)
