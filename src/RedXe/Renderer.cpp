#include "Renderer.h"

#include <array>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <string_view>

namespace
{
constexpr std::string_view kShaderSource = R"(
cbuffer FrameConstants : register(b0)
{
    float4 transform;
};

struct VertexInput
{
    float2 position : POSITION;
    float4 color : COLOR;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PixelInput VertexMain(VertexInput input)
{
    PixelInput output;
    const float2 rotated = float2(
        input.position.x * transform.x - input.position.y * transform.y,
        input.position.x * transform.y + input.position.y * transform.x);
    output.position = float4(rotated.x * transform.z, rotated.y * transform.w, 0.0f, 1.0f);
    output.color = input.color;
    return output;
}

float4 PixelMain(PixelInput input) : SV_TARGET
{
    return input.color;
}
)";

HRESULT CompileShader(std::string_view entryPoint, std::string_view profile,
                      wil::com_ptr_nothrow<ID3DBlob>& bytecode) noexcept
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    wil::com_ptr_nothrow<ID3DBlob> diagnostics;
    const HRESULT result =
        D3DCompile(kShaderSource.data(), kShaderSource.size(), "RedXe.embedded.hlsl", nullptr, nullptr,
                   entryPoint.data(), profile.data(), flags, 0, bytecode.put(), diagnostics.put());
    if (diagnostics)
    {
        OutputDebugStringA(static_cast<const char*>(diagnostics->GetBufferPointer()));
    }
    return result;
}
} // namespace

HRESULT Renderer::Initialize(HWND window, bool forceWarp, float rotationRadiansPerSecond) noexcept
{
    if (!window || !std::isfinite(rotationRadiansPerSecond) || rotationRadiansPerSecond <= 0.0f)
    {
        return E_INVALIDARG;
    }

    _window = window;
    _forceWarp = forceWarp;
    _rotationRadiansPerSecond = rotationRadiansPerSecond;
    return CreateDeviceResources();
}

HRESULT Renderer::CreateDeviceResources() noexcept
{
    ReleaseDeviceResources();

    HRESULT result = CreateDevice(_forceWarp);
    if (FAILED(result) && !_forceWarp)
    {
        OutputDebugStringW(L"Hardware Direct3D initialization failed; trying WARP.\n");
        result = CreateDevice(true);
    }
    if (FAILED(result))
    {
        return result;
    }

    result = CreateSwapChain();
    if (FAILED(result))
    {
        return result;
    }

    result = CreatePipeline();
    if (FAILED(result))
    {
        return result;
    }

    RECT client{};
    if (!GetClientRect(_window, &client))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const UINT width = static_cast<UINT>(client.right - client.left);
    const UINT height = static_cast<UINT>(client.bottom - client.top);
    if (width == 0 || height == 0)
    {
        _suspended = true;
        return S_OK;
    }

    return CreateRenderTarget(width, height);
}

HRESULT Renderer::CreateDevice(bool useWarp) noexcept
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    constexpr std::array featureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    auto create = [&](UINT requestedFlags) noexcept
    {
        D3D_FEATURE_LEVEL selectedLevel{};
        return D3D11CreateDevice(nullptr, useWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 requestedFlags, featureLevels.data(), static_cast<UINT>(featureLevels.size()),
                                 D3D11_SDK_VERSION, _device.put(), &selectedLevel, _context.put());
    };

    HRESULT result = create(flags);
#if defined(_DEBUG)
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
    {
        OutputDebugStringW(L"Direct3D debug layer is unavailable; continuing without it.\n");
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = create(flags);
    }
#endif
    return result;
}

HRESULT Renderer::CreateSwapChain() noexcept
{
    wil::com_ptr_nothrow<IDXGIDevice> dxgiDevice;
    HRESULT result = _device.query_to(dxgiDevice.put());
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGIAdapter> adapter;
    result = dxgiDevice->GetAdapter(adapter.put());
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGIFactory2> factory;
    result = adapter->GetParent(IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    result = factory->CreateSwapChainForHwnd(_device.get(), _window, &description, nullptr, nullptr, _swapChain.put());
    if (FAILED(result))
    {
        return result;
    }

    return factory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER);
}

HRESULT Renderer::CreatePipeline() noexcept
{
    wil::com_ptr_nothrow<ID3DBlob> vertexBytecode;
    HRESULT result = CompileShader("VertexMain", "vs_5_0", vertexBytecode);
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<ID3DBlob> pixelBytecode;
    result = CompileShader("PixelMain", "ps_5_0", pixelBytecode);
    if (FAILED(result))
    {
        return result;
    }

    result = _device->CreateVertexShader(vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(), nullptr,
                                         _vertexShader.put());
    if (FAILED(result))
    {
        return result;
    }

    result = _device->CreatePixelShader(pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(), nullptr,
                                        _pixelShader.put());
    if (FAILED(result))
    {
        return result;
    }

    constexpr std::array layout{
        D3D11_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        D3D11_INPUT_ELEMENT_DESC{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    result =
        _device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()), vertexBytecode->GetBufferPointer(),
                                   vertexBytecode->GetBufferSize(), _inputLayout.put());
    if (FAILED(result))
    {
        return result;
    }

    constexpr std::array vertices{
        Vertex{{0.0f, 0.72f}, {1.0f, 0.2f, 0.16f, 1.0f}},
        Vertex{{0.68f, -0.52f}, {0.15f, 0.9f, 0.35f, 1.0f}},
        Vertex{{-0.68f, -0.52f}, {0.18f, 0.45f, 1.0f, 1.0f}},
    };

    D3D11_BUFFER_DESC vertexDescription{};
    vertexDescription.ByteWidth = static_cast<UINT>(sizeof(vertices));
    vertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
    vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexData{};
    vertexData.pSysMem = vertices.data();
    result = _device->CreateBuffer(&vertexDescription, &vertexData, _vertexBuffer.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_BUFFER_DESC constantDescription{};
    constantDescription.ByteWidth = sizeof(FrameConstants);
    constantDescription.Usage = D3D11_USAGE_DYNAMIC;
    constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return _device->CreateBuffer(&constantDescription, nullptr, _frameConstants.put());
}

HRESULT Renderer::CreateRenderTarget(UINT width, UINT height) noexcept
{
    wil::com_ptr_nothrow<ID3D11Texture2D> backBuffer;
    HRESULT result = _swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put()));
    if (FAILED(result))
    {
        return result;
    }

    result = _device->CreateRenderTargetView(backBuffer.get(), nullptr, _renderTarget.put());
    if (FAILED(result))
    {
        return result;
    }

    _width = width;
    _height = height;
    _suspended = false;

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    _context->RSSetViewports(1, &viewport);
    return S_OK;
}

HRESULT Renderer::Resize(UINT width, UINT height) noexcept
{
    if (!_swapChain)
    {
        return E_UNEXPECTED;
    }

    if (width == 0 || height == 0)
    {
        _width = 0;
        _height = 0;
        _suspended = true;
        return S_OK;
    }

    if (width == _width && height == _height && _renderTarget)
    {
        _suspended = false;
        return S_OK;
    }

    _context->OMSetRenderTargets(0, nullptr, nullptr);
    _renderTarget.reset();
    const HRESULT result = _swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (IsDeviceLost(result))
    {
        return RecoverDevice();
    }
    if (FAILED(result))
    {
        return result;
    }

    return CreateRenderTarget(width, height);
}

HRESULT Renderer::Render(float elapsedSeconds) noexcept
{
    if (_suspended)
    {
        return S_OK;
    }
    if (!_context || !_swapChain || !_renderTarget)
    {
        return E_UNEXPECTED;
    }

    FrameConstants constants{};
    const float angle = elapsedSeconds * _rotationRadiansPerSecond;
    constants.cosine = std::cos(angle);
    constants.sine = std::sin(angle);
    constants.scaleX = 1.0f;
    constants.scaleY = 1.0f;
    if (_width > _height)
    {
        constants.scaleX = static_cast<float>(_height) / static_cast<float>(_width);
    }
    else if (_height > _width)
    {
        constants.scaleY = static_cast<float>(_width) / static_cast<float>(_height);
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result = _context->Map(_frameConstants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (IsDeviceLost(result))
    {
        return RecoverDevice();
    }
    if (FAILED(result))
    {
        return result;
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    _context->Unmap(_frameConstants.get(), 0);

    constexpr std::array clearColor{0.025f, 0.035f, 0.075f, 1.0f};
    ID3D11RenderTargetView* renderTargets[] = {_renderTarget.get()};
    _context->OMSetRenderTargets(1, renderTargets, nullptr);
    _context->ClearRenderTargetView(_renderTarget.get(), clearColor.data());

    constexpr UINT stride = sizeof(Vertex);
    constexpr UINT offset = 0;
    ID3D11Buffer* vertexBuffers[] = {_vertexBuffer.get()};
    ID3D11Buffer* constantBuffers[] = {_frameConstants.get()};
    _context->IASetInputLayout(_inputLayout.get());
    _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    _context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
    _context->VSSetShader(_vertexShader.get(), nullptr, 0);
    _context->VSSetConstantBuffers(0, 1, constantBuffers);
    _context->PSSetShader(_pixelShader.get(), nullptr, 0);
    _context->Draw(3, 0);

    result = _swapChain->Present(1, 0);
    if (IsDeviceLost(result))
    {
        return RecoverDevice();
    }
    return result == DXGI_STATUS_OCCLUDED ? S_OK : result;
}

HRESULT Renderer::RecoverDevice() noexcept
{
    OutputDebugStringW(L"Direct3D device was lost; rebuilding graphics resources.\n");
    return CreateDeviceResources();
}

void Renderer::ReleaseDeviceResources() noexcept
{
    if (_context)
    {
        _context->ClearState();
        _context->Flush();
    }

    _frameConstants.reset();
    _vertexBuffer.reset();
    _inputLayout.reset();
    _pixelShader.reset();
    _vertexShader.reset();
    _renderTarget.reset();
    _swapChain.reset();
    _context.reset();
    _device.reset();
    _width = 0;
    _height = 0;
    _suspended = true;
}

bool Renderer::IsDeviceLost(HRESULT result) noexcept
{
    return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
           result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}
