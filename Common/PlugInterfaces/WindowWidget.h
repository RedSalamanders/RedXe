#pragma once

#include "Widget.h"

#include <cstdint>
#include <windows.h>

struct RedXeWindowWidgetAttachContext final
{
    std::uint32_t sizeBytes;
    HWND container;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t dpi;
};

struct RedXeWindowWidgetSizeContext final
{
    std::uint32_t sizeBytes;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t dpi;
};

// Native child-window rendering mechanism. The host owns the container; the plugin owns every child it creates.
interface __declspec(uuid("D324CF49-A71A-4AE9-AC25-D2988F302625")) __declspec(novtable) IRedXeWindowWidget : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Attach(const RedXeWindowWidgetAttachContext* context) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Resize(const RedXeWindowWidgetSizeContext* context) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept = 0;
    virtual void STDMETHODCALLTYPE Detach() noexcept = 0;
};
