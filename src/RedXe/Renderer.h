#pragma once

#include "PlugInterfaces/Widget.h"

#include <cstddef>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

class PluginManager;

class Renderer final
{
  public:
    Renderer() noexcept;
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    HRESULT Initialize(HWND window, bool forceWarp, PluginManager& pluginManager) noexcept;
    HRESULT Resize(UINT width, UINT height) noexcept;
    HRESULT Render(float elapsedSeconds, float deltaSeconds) noexcept;
    [[nodiscard]] std::size_t LastFrameWidgetCount() const noexcept;
    [[nodiscard]] std::size_t LastFrameSuccessfulWidgetCount() const noexcept;

  private:
    class FrameBuilder final : public IRedXeFrameBuilder
    {
      public:
        explicit FrameBuilder(Renderer& renderer) noexcept;

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override;
        ULONG STDMETHODCALLTYPE AddRef() noexcept override;
        ULONG STDMETHODCALLTYPE Release() noexcept override;
        HRESULT STDMETHODCALLTYPE DrawTriangle(const RedXeTriangleCommand* command) noexcept override;

      private:
        Renderer& _renderer;
    };

    HRESULT CreateDeviceResources() noexcept;
    HRESULT CreateDevice(bool useWarp) noexcept;
    HRESULT CreateSwapChain() noexcept;
    HRESULT CreatePipeline() noexcept;
    HRESULT CreateRenderTarget(UINT width, UINT height) noexcept;
    HRESULT RecoverDevice() noexcept;
    HRESULT DrawTriangle(const RedXeTriangleCommand& command) noexcept;
    void ReleaseDeviceResources() noexcept;

    static bool IsDeviceLost(HRESULT result) noexcept;

    HWND _window = nullptr;
    PluginManager* _pluginManager = nullptr;
    bool _forceWarp = false;
    bool _suspended = true;
    bool _buildingWidget = false;
    UINT _width = 0;
    UINT _height = 0;
    std::size_t _lastFrameWidgetCount = 0;
    std::size_t _lastFrameSuccessfulWidgetCount = 0;
    FrameBuilder _frameBuilder;

    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _context;
    wil::com_ptr_nothrow<IDXGISwapChain1> _swapChain;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> _renderTarget;
    wil::com_ptr_nothrow<ID3D11VertexShader> _vertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _pixelShader;
    wil::com_ptr_nothrow<ID3D11InputLayout> _inputLayout;
    wil::com_ptr_nothrow<ID3D11Buffer> _vertexBuffer;
};
