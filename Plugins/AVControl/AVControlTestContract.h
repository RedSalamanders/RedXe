#pragma once
#include "../../Common/PlugInterfaces/Factory.h"
#include "AVControlProtocol.h"
#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_AV_TEST_API __declspec(dllexport)
#else
#define REDXE_AV_TEST_API
#endif
extern "C"
{
    // Explicit test-only construction, before any provider is alive; no settings/environment switch enables this.
    REDXE_AV_TEST_API HRESULT __stdcall RedXeAVControlUseSyntheticBackend(BOOL enabled) noexcept;
    REDXE_AV_TEST_API HRESULT __stdcall RedXeAVControlTestSnapshot(AVControl::Inventory* inventory,
                                                                   uint32_t bytes) noexcept;
}
#undef REDXE_AV_TEST_API
