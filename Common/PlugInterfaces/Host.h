#pragma once

#include "Data.h"

#include <unknwn.h>

// Services supplied to plugins by the RedXe host.
interface __declspec(uuid("D00BE2C2-10C4-4D8D-8097-4C26DFC29A19")) __declspec(novtable) IRedXeHost : IUnknown
{
    // Returns shared host-managed access to one data-source plugin.
    virtual HRESULT STDMETHODCALLTYPE GetDataProvider(const char* providerId,
                                                      IRedXeDataProvider** provider) noexcept = 0;
};
