#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/GpuWidget.h"
#include "RotatingTrianglePixelShader.h"
#include "RotatingTriangleVertexShader.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.rotating-triangle";
constexpr char kWidgetTypeId[] = "rotating-triangle";

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"Rotating Triangle",
        L"Animated colored triangle widget used to validate the RedXe GPU-widget host.",
        L"RedSalamanders",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kWidgetTypeId,
        L"Rotating Triangle",
        L"A continuously animated Direct3D 11 widget.",
        1168.0f,
        296.0f,
        160.0f,
        120.0f,
        RedXeWidgetFlagContinuousAnimation,
    },
};

struct TriangleVertex final
{
    float position[2];
};

struct TriangleConstants final
{
    float cosine;
    float sine;
    float scaleX;
    float scaleY;
    std::uint32_t colorOffset;
    std::uint32_t padding[3];
};

static_assert(sizeof(TriangleConstants) == 32);

constexpr std::array kVertices{
    TriangleVertex{{0.0f, 0.72f}},
    TriangleVertex{{0.68f, -0.52f}},
    TriangleVertex{{-0.68f, -0.52f}},
};

class TriangleDeviceResources final
{
  public:
    [[nodiscard]] HRESULT Initialize(ID3D11Device* device) noexcept
    {
        if (!device)
        {
            return E_POINTER;
        }
        if (_deviceIdentity == device && _constantBuffer)
        {
            return S_OK;
        }

        Reset();

        wil::com_ptr_nothrow<ID3D11VertexShader> vertexShader;
        HRESULT result = device->CreateVertexShader(
            g_RotatingTriangleVertexShader, sizeof(g_RotatingTriangleVertexShader), nullptr, vertexShader.put());
        if (FAILED(result))
        {
            return result;
        }

        wil::com_ptr_nothrow<ID3D11PixelShader> pixelShader;
        result = device->CreatePixelShader(g_RotatingTrianglePixelShader, sizeof(g_RotatingTrianglePixelShader),
                                           nullptr, pixelShader.put());
        if (FAILED(result))
        {
            return result;
        }

        constexpr std::array layout{
            D3D11_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        wil::com_ptr_nothrow<ID3D11InputLayout> inputLayout;
        result =
            device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()), g_RotatingTriangleVertexShader,
                                      sizeof(g_RotatingTriangleVertexShader), inputLayout.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_BUFFER_DESC vertexDescription{};
        vertexDescription.ByteWidth = sizeof(kVertices);
        vertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
        vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertexData{};
        vertexData.pSysMem = kVertices.data();
        wil::com_ptr_nothrow<ID3D11Buffer> vertexBuffer;
        result = device->CreateBuffer(&vertexDescription, &vertexData, vertexBuffer.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth = sizeof(TriangleConstants);
        constantDescription.Usage = D3D11_USAGE_DYNAMIC;
        constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        wil::com_ptr_nothrow<ID3D11Buffer> constantBuffer;
        result = device->CreateBuffer(&constantDescription, nullptr, constantBuffer.put());
        if (FAILED(result))
        {
            return result;
        }

        _deviceIdentity = device;
        _vertexShader = std::move(vertexShader);
        _pixelShader = std::move(pixelShader);
        _inputLayout = std::move(inputLayout);
        _vertexBuffer = std::move(vertexBuffer);
        _constantBuffer = std::move(constantBuffer);
        return S_OK;
    }

    void Reset() noexcept
    {
        _constantBuffer.reset();
        _vertexBuffer.reset();
        _inputLayout.reset();
        _pixelShader.reset();
        _vertexShader.reset();
        _deviceIdentity = nullptr;
    }

    [[nodiscard]] HRESULT Render(ID3D11DeviceContext* context, const TriangleConstants& constants) noexcept
    {
        if (!context || !_constantBuffer)
        {
            return E_UNEXPECTED;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT result = context->Map(_constantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
        {
            return result;
        }
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(_constantBuffer.get(), 0);

        constexpr UINT stride = sizeof(TriangleVertex);
        constexpr UINT offset = 0;
        ID3D11Buffer* vertexBuffers[] = {_vertexBuffer.get()};
        ID3D11Buffer* constantBuffers[] = {_constantBuffer.get()};
        context->IASetInputLayout(_inputLayout.get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
        context->VSSetShader(_vertexShader.get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, constantBuffers);
        context->PSSetShader(_pixelShader.get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->RSSetState(nullptr);
        context->OMSetBlendState(nullptr, nullptr, UINT_MAX);
        context->OMSetDepthStencilState(nullptr, 0);
        context->Draw(static_cast<UINT>(kVertices.size()), 0);
        return S_OK;
    }

  private:
    ID3D11Device* _deviceIdentity = nullptr;
    wil::com_ptr_nothrow<ID3D11VertexShader> _vertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _pixelShader;
    wil::com_ptr_nothrow<ID3D11InputLayout> _inputLayout;
    wil::com_ptr_nothrow<ID3D11Buffer> _vertexBuffer;
    wil::com_ptr_nothrow<ID3D11Buffer> _constantBuffer;
};

[[nodiscard]] std::uint32_t HashInstanceId(const char* instanceId) noexcept
{
    std::uint32_t hash = 2166136261U;
    for (const char* character = instanceId; *character != '\0'; ++character)
    {
        hash ^= static_cast<std::uint32_t>(*character);
        hash *= 16777619U;
    }
    return hash;
}

class RotatingTriangleWidget final : public IRedXeWidget, public IRedXeGpuWidget
{
  public:
    RotatingTriangleWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner,
                           TriangleDeviceResources& resources, std::uint32_t instanceHash) noexcept
        : _providerOwner(std::move(providerOwner)), _resources(&resources),
          _speed((instanceHash & 1U) == 0U ? 0.55f + static_cast<float>((instanceHash >> 1U) % 5U) * 0.11f
                                           : -0.55f - static_cast<float>((instanceHash >> 1U) % 5U) * 0.11f),
          _phase(static_cast<float>(instanceHash % 6283U) * 0.001f), _colorOffset(instanceHash % 3U)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeWidget))
        {
            *result = static_cast<IRedXeWidget*>(this);
        }
        else if (interfaceId == __uuidof(IRedXeGpuWidget))
        {
            *result = static_cast<IRedXeGpuWidget*>(this);
        }
        else
        {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return ++_references;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        const ULONG references = --_references;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept override
    {
        if (!context)
        {
            return E_POINTER;
        }
        if (context->sizeBytes < sizeof(RedXeGpuDeviceContext) || !context->device)
        {
            return E_INVALIDARG;
        }
        return _resources->Initialize(context->device);
    }

    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        _resources->Reset();
    }

    HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept override
    {
        if (!context || !context->widget)
        {
            return E_POINTER;
        }
        const RedXeWidgetFrameContext& widget = *context->widget;
        if (context->sizeBytes < sizeof(RedXeGpuFrameContext) || widget.sizeBytes < sizeof(RedXeWidgetFrameContext) ||
            !context->deviceContext || widget.widthPixels == 0 || widget.heightPixels == 0 ||
            !std::isfinite(widget.elapsedSeconds) || !std::isfinite(widget.deltaSeconds) || widget.deltaSeconds < 0.0f)
        {
            return E_INVALIDARG;
        }

        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (widget.widthPixels > widget.heightPixels)
        {
            scaleX = static_cast<float>(widget.heightPixels) / static_cast<float>(widget.widthPixels);
        }
        else if (widget.heightPixels > widget.widthPixels)
        {
            scaleY = static_cast<float>(widget.widthPixels) / static_cast<float>(widget.heightPixels);
        }

        const float angle = widget.elapsedSeconds * _speed + _phase;
        const TriangleConstants constants{
            std::cos(angle), std::sin(angle), scaleX, scaleY, _colorOffset, {},
        };
        return _resources->Render(context->deviceContext, constants);
    }

  private:
    std::atomic<ULONG> _references{1};
    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    TriangleDeviceResources* _resources;
    float _speed;
    float _phase;
    std::uint32_t _colorOffset;
};

class RotatingTriangleProvider final : public IRedXeWidgetProvider
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeWidgetProvider))
        {
            *result = static_cast<IRedXeWidgetProvider*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return ++_references;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        const ULONG references = --_references;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                             std::uint32_t* count) noexcept override
    {
        if (descriptors)
        {
            *descriptors = nullptr;
        }
        if (count)
        {
            *count = 0;
        }
        if (!descriptors || !count)
        {
            return E_POINTER;
        }
        *descriptors = kWidgetTypes.data();
        *count = static_cast<std::uint32_t>(kWidgetTypes.size());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateWidget(const char* typeId, const char* instanceId,
                                           IRedXeWidget** widget) noexcept override
    {
        if (!widget)
        {
            return E_POINTER;
        }
        *widget = nullptr;
        if (!typeId || !instanceId || instanceId[0] == '\0')
        {
            return E_INVALIDARG;
        }
        if (!RedXeAsciiEqualsIgnoreCase(typeId, kWidgetTypeId))
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }

        wil::com_ptr_nothrow<IRedXeWidgetProvider> providerOwner;
        HRESULT result = QueryInterface(__uuidof(IRedXeWidgetProvider), reinterpret_cast<void**>(providerOwner.put()));
        if (FAILED(result))
        {
            return result;
        }

        auto* created =
            new (std::nothrow) RotatingTriangleWidget(std::move(providerOwner), _resources, HashInstanceId(instanceId));
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    std::atomic<ULONG> _references{1};
    TriangleDeviceResources _resources;
};

HRESULT CreateRotatingTriangleProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost*,
                                       void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
    }

    const HRESULT configurationResult = RedXeValidateEmptyNormalizedConfiguration(options);
    if (FAILED(configurationResult))
    {
        return configurationResult;
    }

    auto* provider = new (std::nothrow) RotatingTriangleProvider();
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = provider;
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateRotatingTriangleProvider},
};
} // namespace

extern "C" HRESULT __stdcall RedXeCreate(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                         const char* pluginId, void** result) noexcept
{
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<std::uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, std::uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<std::uint32_t>(kMetadata.size()), metadata,
                                         count);
}
