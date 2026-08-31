#pragma once

#include "Widget.h"

#include <cstdint>
#include <d3d11.h>
#include <dxgiformat.h>

struct RedXeGpuDeviceContext final
{
    std::uint32_t sizeBytes;
    ID3D11Device* device;
    DXGI_FORMAT targetFormat;
    D3D_FEATURE_LEVEL featureLevel;
};

struct RedXeGpuFrameContext final
{
    std::uint32_t sizeBytes;
    const RedXeWidgetFrameContext* widget;
    ID3D11DeviceContext* deviceContext;
    D3D11_VIEWPORT viewport;
};

// Direct3D 11 rendering path for widgets that need unrestricted GPU drawing.
interface __declspec(uuid("DBEED29C-63EB-409E-816B-F4BDC5EF7AA9")) __declspec(novtable) IRedXeGpuWidget : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept = 0;
    virtual void STDMETHODCALLTYPE OnDeviceLost() noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept = 0;
};
