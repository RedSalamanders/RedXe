#pragma once

#include <cstdint>
#include <unknwn.h>

enum RedXeWidgetRenderPath : std::uint32_t
{
    RedXeWidgetRenderPathStandard = 1,
    RedXeWidgetRenderPathGpu = 2,
    RedXeWidgetRenderPathWindow = 3,
};

enum RedXeWidgetFlags : std::uint32_t
{
    RedXeWidgetFlagNone = 0,
    RedXeWidgetFlagContinuousAnimation = 1U << 0U,
};

struct RedXeWidgetTypeDescriptor final
{
    std::uint32_t sizeBytes;
    const wchar_t* typeId;
    const wchar_t* displayName;
    const wchar_t* description;
    float defaultWidth;
    float defaultHeight;
    float minimumWidth;
    float minimumHeight;
    std::uint32_t renderPath;
    std::uint32_t flags;
    std::uint32_t reserved[8];
};

struct RedXeWidgetFrameContext final
{
    std::uint32_t sizeBytes;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t dpi;
    float elapsedSeconds;
    float deltaSeconds;
    std::uint32_t reserved[8];
};

struct RedXeColorVertex final
{
    float position[2];
    float color[4];
};

struct RedXeTriangleCommand final
{
    std::uint32_t sizeBytes;
    RedXeColorVertex vertices[3];
    std::uint32_t reserved[4];
};

struct __declspec(uuid("027D19CE-187E-486C-AEA3-B8B4F75D3F80")) __declspec(novtable) IRedXeWidgetTypeSink : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE AddWidgetType(const RedXeWidgetTypeDescriptor* descriptor) noexcept = 0;
};

struct __declspec(uuid("45B1E983-0F49-4F1B-ACC2-90E2FF19C3FE")) __declspec(novtable) IRedXeFrameBuilder : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE DrawTriangle(const RedXeTriangleCommand* command) noexcept = 0;
};

struct __declspec(uuid("6E4C2E10-D546-4A1C-A478-068EAB2B02E5")) __declspec(novtable) IRedXeWidget : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE BuildFrame(const RedXeWidgetFrameContext* context,
                                                 IRedXeFrameBuilder* frameBuilder) noexcept = 0;
};

struct __declspec(uuid("436A7DF3-DB73-442B-99FE-63837F24BA75")) __declspec(novtable) IRedXeWidgetProvider : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE EnumerateWidgetTypes(IRedXeWidgetTypeSink* sink) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWidget(const wchar_t* typeId, const wchar_t* instanceId,
                                                   IRedXeWidget** widget) noexcept = 0;
};
