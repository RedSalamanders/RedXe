#pragma once

#include <cstdint>
#include <unknwn.h>

enum RedXeWidgetFlags : std::uint32_t
{
    RedXeWidgetFlagNone = 0,
    RedXeWidgetFlagContinuousAnimation = 1U << 0U,
};

struct RedXeWidgetTypeDescriptor final
{
    std::uint32_t sizeBytes;
    const char* typeId;
    const wchar_t* displayName;
    const wchar_t* description;
    float defaultWidth;
    float defaultHeight;
    float minimumWidth;
    float minimumHeight;
    std::uint32_t flags;
};

struct RedXeWidgetFrameContext final
{
    std::uint32_t sizeBytes;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t dpi;
    float elapsedSeconds;
    float deltaSeconds;
};

// Generic widget identity and lifetime root. Rendering mechanisms are negotiated through QueryInterface.
interface __declspec(uuid("2C66DE33-08D1-4A0C-890C-38521F142AA0")) __declspec(novtable) IRedXeWidget : IUnknown{};

interface __declspec(uuid("231AC0E8-1204-4BFF-BCEA-7CACF11F439D")) __declspec(novtable) IRedXeWidgetProvider : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                                     std::uint32_t* count) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWidget(const char* typeId, const char* instanceId,
                                                   IRedXeWidget** widget) noexcept = 0;
};
