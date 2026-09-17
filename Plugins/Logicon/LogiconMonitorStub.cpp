// Release builds keep the Logicon Monitor catalogued and schema-accepted so both shipped templates parse, but they
// ship no tile: the provider lists the type and refuses every instance, which the host turns into a placeholder.

#include "LogiconMonitor.h"
#include "LogiconSettings.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"

#include <array>
#include <new>

namespace Logicon
{
namespace
{
constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kMonitorWidgetTypeId,
        L"Logicon Monitor",
        L"Developer view of the Logicon service; available in Debug builds only.",
        720.0f,
        420.0f,
        360.0f,
        240.0f,
        RedXeWidgetFlagNone,
    },
};

class MonitorStubProvider final : public RedXeComObject<MonitorStubProvider, IRedXeWidgetProvider>
{
  public:
    HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                             uint32_t* count) noexcept override
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
        *count = static_cast<uint32_t>(kWidgetTypes.size());
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
        if (!RedXeAsciiEqualsIgnoreCase(typeId, kMonitorWidgetTypeId))
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
};
} // namespace

HRESULT CreateMonitorProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost*,
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
    auto* provider = new (std::nothrow) MonitorStubProvider();
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = provider;
    return S_OK;
}
} // namespace Logicon
