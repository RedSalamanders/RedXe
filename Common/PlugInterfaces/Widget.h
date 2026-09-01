#pragma once

#include <cstdint>
#include <d3d11.h>
#include <dxgiformat.h>
#include <unknwn.h>
#include <windows.h>

// Every sizeBytes field must equal the current record's sizeof value.

// Scheduling behavior advertised by a widget type.
enum RedXeWidgetFlags : std::uint32_t
{
    RedXeWidgetFlagNone = 0,
    RedXeWidgetFlagContinuousAnimation = 1U << 0U,
};

// Module-owned metadata for one creatable widget type.
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

// Borrowed pixel size and timing for one widget render call.
struct RedXeWidgetFrameContext final
{
    std::uint32_t sizeBytes;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t dpi;
    float elapsedSeconds;
    float deltaSeconds;
};

// Borrowed D3D11 device details supplied during device-resource creation.
struct RedXeGpuDeviceContext final
{
    std::uint32_t sizeBytes;
    ID3D11Device* device;
    DXGI_FORMAT targetFormat;
    D3D_FEATURE_LEVEL featureLevel;
};

// Borrowed D3D11 state supplied for one widget render call.
struct RedXeGpuFrameContext final
{
    std::uint32_t sizeBytes;
    const RedXeWidgetFrameContext* widget;
    ID3D11DeviceContext* deviceContext;
    D3D11_VIEWPORT viewport;
};

// Borrowed host child-window details supplied during attachment.
struct RedXeWindowWidgetAttachContext final
{
    std::uint32_t sizeBytes;
    HWND container;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t dpi;
};

// Borrowed pixel size and DPI supplied after container resize.
struct RedXeWindowWidgetSizeContext final
{
    std::uint32_t sizeBytes;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t dpi;
};

inline constexpr std::uint32_t kRedXeMaximumScheduledFrameDelayMilliseconds = 86'400'000U;

// Generic widget identity and visibility; rendering mechanisms are sibling interfaces.
interface __declspec(uuid("62DB9FB4-AF7B-47C0-BBF9-B7D5CA535502")) __declspec(novtable) IRedXeWidget : IUnknown
{
    // Enables visible work; FALSE quiesces timers, subscriptions, and background activity.
    virtual HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept = 0;
};

// Enumerates widget types and creates independent widget instances.
interface __declspec(uuid("231AC0E8-1204-4BFF-BCEA-7CACF11F439D")) __declspec(novtable) IRedXeWidgetProvider : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                                     std::uint32_t* count) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWidget(const char* typeId, const char* instanceId,
                                                   IRedXeWidget** widget) noexcept = 0;
};

// Direct3D 11 rendering mechanism.
interface __declspec(uuid("DBEED29C-63EB-409E-816B-F4BDC5EF7AA9")) __declspec(novtable) IRedXeGpuWidget : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept = 0;
    virtual void STDMETHODCALLTYPE OnDeviceLost() noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept = 0;
};

// Optional low-cadence frame deadline mechanism.
interface __declspec(uuid("1B6B4F9E-5421-4B4E-BC2D-190EE6CE86CB")) __declspec(novtable) IRedXeScheduledWidget : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetNextFrameDelayMilliseconds(std::uint32_t* delayMilliseconds) noexcept = 0;
};

// Native child-window rendering mechanism; the host owns the container.
interface __declspec(uuid("3219FA78-260B-416A-BB76-6331DBF30593")) __declspec(novtable) IRedXeWindowWidget : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Attach(const RedXeWindowWidgetAttachContext* context) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Resize(const RedXeWindowWidgetSizeContext* context) noexcept = 0;
    virtual void STDMETHODCALLTYPE Detach() noexcept = 0;
};
