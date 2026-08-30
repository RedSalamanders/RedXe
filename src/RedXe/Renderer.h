#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

class Renderer final
{
  public:
    Renderer() = default;
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    HRESULT Initialize(HWND window, bool forceWarp, float rotationRadiansPerSecond) noexcept;
    HRESULT Resize(UINT width, UINT height) noexcept;
    HRESULT Render(float elapsedSeconds) noexcept;

  private:
    struct Vertex
    {
        float position[2];
        float color[4];
    };

    struct alignas(16) FrameConstants
    {
        float cosine;
        float sine;
        float scaleX;
        float scaleY;
    };

    HRESULT CreateDeviceResources() noexcept;
    HRESULT CreateDevice(bool useWarp) noexcept;
    HRESULT CreateSwapChain() noexcept;
    HRESULT CreatePipeline() noexcept;
    HRESULT CreateRenderTarget(UINT width, UINT height) noexcept;
    HRESULT RecoverDevice() noexcept;
    void ReleaseDeviceResources() noexcept;

    static bool IsDeviceLost(HRESULT result) noexcept;

    HWND _window = nullptr;
    bool _forceWarp = false;
    bool _suspended = true;
    UINT _width = 0;
    UINT _height = 0;
    float _rotationRadiansPerSecond = 0.72f;

    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _context;
    wil::com_ptr_nothrow<IDXGISwapChain1> _swapChain;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> _renderTarget;
    wil::com_ptr_nothrow<ID3D11VertexShader> _vertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _pixelShader;
    wil::com_ptr_nothrow<ID3D11InputLayout> _inputLayout;
    wil::com_ptr_nothrow<ID3D11Buffer> _vertexBuffer;
    wil::com_ptr_nothrow<ID3D11Buffer> _frameConstants;
};
