#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>

namespace
{
constexpr wchar_t kPluginId[] = L"builtin.rotating-triangle";
constexpr wchar_t kWidgetTypeId[] = L"rotating-triangle";

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"Rotating Triangle",
        L"Animated colored triangle widget used to validate the RedXe plugin host.",
        L"RedSalamanders",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
        {},
    },
};

constexpr RedXeWidgetTypeDescriptor kWidgetType{
    sizeof(RedXeWidgetTypeDescriptor),
    kWidgetTypeId,
    L"Rotating Triangle",
    L"A continuously animated RGB triangle rendered through the standard frame builder.",
    1168.0f,
    296.0f,
    160.0f,
    120.0f,
    RedXeWidgetRenderPathStandard,
    RedXeWidgetFlagContinuousAnimation,
    {},
};

[[nodiscard]] std::uint32_t HashInstanceId(const wchar_t* instanceId) noexcept
{
    std::uint32_t hash = 2166136261U;
    for (const wchar_t* character = instanceId; *character != L'\0'; ++character)
    {
        hash ^= static_cast<std::uint32_t>(*character);
        hash *= 16777619U;
    }
    return hash;
}

class RotatingTriangleWidget final : public IRedXeWidget
{
  public:
    explicit RotatingTriangleWidget(std::uint32_t instanceHash) noexcept
        : _speed((instanceHash & 1U) == 0U ? 0.55f + static_cast<float>((instanceHash >> 1U) % 5U) * 0.11f
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

    HRESULT STDMETHODCALLTYPE BuildFrame(const RedXeWidgetFrameContext* context,
                                         IRedXeFrameBuilder* frameBuilder) noexcept override
    {
        if (!context || !frameBuilder)
        {
            return E_POINTER;
        }
        if (context->sizeBytes < offsetof(RedXeWidgetFrameContext, reserved) || context->widthPixels == 0 ||
            context->heightPixels == 0 || !std::isfinite(context->elapsedSeconds) ||
            !std::isfinite(context->deltaSeconds) || context->deltaSeconds < 0.0f)
        {
            return E_INVALIDARG;
        }

        constexpr std::array<std::array<float, 2>, 3> basePositions{{
            {0.0f, 0.72f},
            {0.68f, -0.52f},
            {-0.68f, -0.52f},
        }};
        constexpr std::array<std::array<float, 4>, 3> baseColors{{
            {1.0f, 0.2f, 0.16f, 1.0f},
            {0.15f, 0.9f, 0.35f, 1.0f},
            {0.18f, 0.45f, 1.0f, 1.0f},
        }};

        const float angle = context->elapsedSeconds * _speed + _phase;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (context->widthPixels > context->heightPixels)
        {
            scaleX = static_cast<float>(context->heightPixels) / static_cast<float>(context->widthPixels);
        }
        else if (context->heightPixels > context->widthPixels)
        {
            scaleY = static_cast<float>(context->widthPixels) / static_cast<float>(context->heightPixels);
        }

        RedXeTriangleCommand command{};
        command.sizeBytes = sizeof(command);
        for (std::size_t index = 0; index < 3; ++index)
        {
            const float x = basePositions[index][0];
            const float y = basePositions[index][1];
            command.vertices[index].position[0] = (x * cosine - y * sine) * scaleX;
            command.vertices[index].position[1] = (x * sine + y * cosine) * scaleY;
            const auto& color = baseColors[(index + _colorOffset) % baseColors.size()];
            for (std::size_t channel = 0; channel < color.size(); ++channel)
            {
                command.vertices[index].color[channel] = color[channel];
            }
        }
        return frameBuilder->DrawTriangle(&command);
    }

  private:
    std::atomic<ULONG> _references{1};
    float _speed;
    float _phase;
    std::size_t _colorOffset;
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

    HRESULT STDMETHODCALLTYPE EnumerateWidgetTypes(IRedXeWidgetTypeSink* sink) noexcept override
    {
        return sink ? sink->AddWidgetType(&kWidgetType) : E_POINTER;
    }

    HRESULT STDMETHODCALLTYPE CreateWidget(const wchar_t* typeId, const wchar_t* instanceId,
                                           IRedXeWidget** widget) noexcept override
    {
        if (!widget)
        {
            return E_POINTER;
        }
        *widget = nullptr;
        if (!typeId || !instanceId || instanceId[0] == L'\0')
        {
            return E_INVALIDARG;
        }
        if (CompareStringOrdinal(typeId, -1, kWidgetTypeId, -1, TRUE) != CSTR_EQUAL)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }

        auto* created = new (std::nothrow) RotatingTriangleWidget(HashInstanceId(instanceId));
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        *widget = created;
        return S_OK;
    }

  private:
    std::atomic<ULONG> _references{1};
};

HRESULT CreateRotatingTriangleProvider(REFIID interfaceId, const RedXeFactoryOptions*, IRedXeHost*,
                                       void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
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

extern "C" __declspec(dllexport) HRESULT __stdcall RedXeCreate(REFIID interfaceId, const RedXeFactoryOptions* options,
                                                               IRedXeHost* host, const wchar_t* pluginId,
                                                               void** result) noexcept
{
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<std::uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" __declspec(dllexport) HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata,
                                                                         std::uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<std::uint32_t>(kMetadata.size()), metadata,
                                         count);
}
