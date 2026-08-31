#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/WindowWidget.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <new>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.gdi-orbit";
constexpr char kWidgetTypeId[] = "gdi-orbit";
constexpr wchar_t kWindowClassName[] = L"RedXe.Plugin.GdiOrbit";
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT kAnimationIntervalMilliseconds = 33;
constexpr double kPi = 3.14159265358979323846;

HINSTANCE g_moduleInstance = nullptr;

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"GDI Orbit",
        L"Low-resource native-window demo with a double-buffered GDI animation.",
        L"RedSalamanders",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kWidgetTypeId,
        L"GDI Orbit",
        L"Animated Xenon orbit rendered by a plugin-owned GDI child window.",
        797.0f,
        308.0f,
        280.0f,
        180.0f,
        RedXeWidgetFlagNone,
    },
};

[[nodiscard]] int ScaleForDpi(int value, UINT dpi) noexcept
{
    return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] HRESULT LastErrorOrFailure() noexcept
{
    const DWORD error = GetLastError();
    return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

[[nodiscard]] HRESULT EnsureWindowClass() noexcept;

class GdiOrbitWidget final : public IRedXeWidget, public IRedXeWindowWidget
{
  public:
    explicit GdiOrbitWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner) noexcept
        : _providerOwner(std::move(providerOwner))
    {
    }

    ~GdiOrbitWidget()
    {
        Detach();
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
        else if (interfaceId == __uuidof(IRedXeWindowWidget))
        {
            *result = static_cast<IRedXeWindowWidget*>(this);
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

    HRESULT STDMETHODCALLTYPE Attach(const RedXeWindowWidgetAttachContext* context) noexcept override
    {
        if (!context)
        {
            return E_POINTER;
        }
        if (context->sizeBytes < sizeof(RedXeWindowWidgetAttachContext) || !context->container ||
            context->widthPixels == 0 || context->heightPixels == 0 || context->dpi == 0 ||
            !IsWindow(context->container))
        {
            return E_INVALIDARG;
        }
        if (_window)
        {
            return E_UNEXPECTED;
        }

        DWORD processId = 0;
        const DWORD threadId = GetWindowThreadProcessId(context->container, &processId);
        if (threadId == 0 || threadId != GetCurrentThreadId() || processId != GetCurrentProcessId())
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID);
        }

        HRESULT result = EnsureWindowClass();
        if (FAILED(result))
        {
            return result;
        }

        _container = context->container;
        const HWND window = CreateWindowExW(
            0, kWindowClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, static_cast<int>(context->widthPixels),
            static_cast<int>(context->heightPixels), context->container, nullptr, g_moduleInstance, this);
        if (!window)
        {
            _container = nullptr;
            return HRESULT_FROM_WIN32(GetLastError());
        }

        result = RebuildDrawingResources(context->widthPixels, context->heightPixels, context->dpi);
        if (FAILED(result))
        {
            _window.reset();
            _container = nullptr;
            return result;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Resize(const RedXeWindowWidgetSizeContext* context) noexcept override
    {
        if (!context)
        {
            return E_POINTER;
        }
        if (context->sizeBytes < sizeof(RedXeWindowWidgetSizeContext) || context->widthPixels == 0 ||
            context->heightPixels == 0 || context->dpi == 0)
        {
            return E_INVALIDARG;
        }
        if (!_window)
        {
            return E_UNEXPECTED;
        }

        if (!SetWindowPos(_window.get(), nullptr, 0, 0, static_cast<int>(context->widthPixels),
                          static_cast<int>(context->heightPixels), SWP_NOACTIVATE | SWP_NOZORDER))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return RebuildDrawingResources(context->widthPixels, context->heightPixels, context->dpi);
    }

    HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept override
    {
        if (!_window)
        {
            return E_UNEXPECTED;
        }

        const bool shouldShow = visible != FALSE;
        if (_visible == shouldShow)
        {
            return S_OK;
        }

        if (shouldShow)
        {
            _resumeTick = GetTickCount64();
            if (SetTimer(_window.get(), kAnimationTimer, kAnimationIntervalMilliseconds, nullptr) == 0)
            {
                return LastErrorOrFailure();
            }
            _timerRunning = true;
            _visible = true;
            ShowWindow(_window.get(), SW_SHOWNA);
            InvalidateRect(_window.get(), nullptr, FALSE);
        }
        else
        {
            if (_timerRunning)
            {
                KillTimer(_window.get(), kAnimationTimer);
                _timerRunning = false;
            }
            if (_visible)
            {
                _elapsedBeforePause += GetTickCount64() - _resumeTick;
            }
            _visible = false;
            ShowWindow(_window.get(), SW_HIDE);
        }
        return S_OK;
    }

    void STDMETHODCALLTYPE Detach() noexcept override
    {
        if (!_window)
        {
            return;
        }

        (void)SetVisible(FALSE);
        _window.reset();
        ReleaseDrawingResources();
        _container = nullptr;
        _elapsedBeforePause = 0;
        _resumeTick = 0;
    }

  private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
        auto* widget = reinterpret_cast<GdiOrbitWidget*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            widget = static_cast<GdiOrbitWidget*>(create->lpCreateParams);
            widget->_window.reset(window);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(widget));
        }

        if (!widget)
        {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_TIMER:
            if (wParam == kAnimationTimer && widget->_visible)
            {
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            widget->Paint();
            return 0;
        case WM_NCDESTROY:
            if (widget->_window.get() == window)
            {
                (void)widget->_window.release();
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wParam, lParam);
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    [[nodiscard]] HRESULT RebuildDrawingResources(std::uint32_t width, std::uint32_t height, UINT dpi) noexcept
    {
        if (!_memoryDc)
        {
            _memoryDc.reset(CreateCompatibleDC(nullptr));
            if (!_memoryDc)
            {
                return LastErrorOrFailure();
            }
        }

        if (!_surface || width != _width || height != _height)
        {
            if (_surface)
            {
                SelectObject(_memoryDc.get(), _previousBitmap);
                _surface.reset();
                _previousBitmap = nullptr;
            }

            BITMAPINFO information{};
            information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            information.bmiHeader.biWidth = static_cast<LONG>(width);
            information.bmiHeader.biHeight = -static_cast<LONG>(height);
            information.bmiHeader.biPlanes = 1;
            information.bmiHeader.biBitCount = 32;
            information.bmiHeader.biCompression = BI_RGB;
            void* pixels = nullptr;
            wil::unique_hbitmap surface{
                CreateDIBSection(_memoryDc.get(), &information, DIB_RGB_COLORS, &pixels, nullptr, 0)};
            if (!surface || !pixels)
            {
                return LastErrorOrFailure();
            }
            _previousBitmap = SelectObject(_memoryDc.get(), surface.get());
            if (!_previousBitmap || _previousBitmap == HGDI_ERROR)
            {
                _previousBitmap = nullptr;
                return LastErrorOrFailure();
            }
            _surface = std::move(surface);
            _width = width;
            _height = height;
        }

        if (_dpi != dpi || !_backgroundBrush)
        {
            _backgroundBrush.reset(CreateSolidBrush(RGB(7, 10, 18)));
            _panelBrush.reset(CreateSolidBrush(RGB(15, 18, 28)));
            _redBrush.reset(CreateSolidBrush(RGB(222, 42, 54)));
            _cyanBrush.reset(CreateSolidBrush(RGB(52, 202, 208)));
            _dimBrush.reset(CreateSolidBrush(RGB(78, 86, 105)));
            _gridPen.reset(CreatePen(PS_SOLID, 1, RGB(21, 27, 40)));
            _orbitPen.reset(CreatePen(PS_SOLID, ScaleForDpi(1, dpi), RGB(64, 71, 88)));
            _redPen.reset(CreatePen(PS_SOLID, ScaleForDpi(2, dpi), RGB(222, 42, 54)));
            _titleFont.reset(CreateFontW(-ScaleForDpi(13, dpi), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI"));
            _symbolFont.reset(CreateFontW(-ScaleForDpi(44, dpi), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                          DEFAULT_PITCH | FF_SWISS, L"Segoe UI"));
            _detailFont.reset(CreateFontW(-ScaleForDpi(11, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                          DEFAULT_PITCH | FF_SWISS, L"Segoe UI"));
            if (!_backgroundBrush || !_panelBrush || !_redBrush || !_cyanBrush || !_dimBrush || !_gridPen ||
                !_orbitPen || !_redPen || !_titleFont || !_symbolFont || !_detailFont)
            {
                return LastErrorOrFailure();
            }
            _dpi = dpi;
        }
        return S_OK;
    }

    void ReleaseDrawingResources() noexcept
    {
        _detailFont.reset();
        _symbolFont.reset();
        _titleFont.reset();
        _redPen.reset();
        _orbitPen.reset();
        _gridPen.reset();
        _dimBrush.reset();
        _cyanBrush.reset();
        _redBrush.reset();
        _panelBrush.reset();
        _backgroundBrush.reset();
        if (_memoryDc && _surface)
        {
            SelectObject(_memoryDc.get(), _previousBitmap);
        }
        _surface.reset();
        _previousBitmap = nullptr;
        _memoryDc.reset();
        _width = 0;
        _height = 0;
        _dpi = 0;
    }

    void Paint() noexcept
    {
        PAINTSTRUCT paint{};
        auto target = wil::BeginPaint(_window.get(), &paint);
        if (!target || !_memoryDc || !_surface || !_backgroundBrush)
        {
            return;
        }

        DrawScene(_memoryDc.get());
        BitBlt(target.get(), 0, 0, static_cast<int>(_width), static_cast<int>(_height), _memoryDc.get(), 0, 0, SRCCOPY);
    }

    void DrawScene(HDC dc) noexcept
    {
        const RECT bounds{0, 0, static_cast<LONG>(_width), static_cast<LONG>(_height)};
        FillRect(dc, &bounds, _backgroundBrush.get());
        SetBkMode(dc, TRANSPARENT);

        HGDIOBJ previousPen = SelectObject(dc, _gridPen.get());
        const int grid = ScaleForDpi(28, _dpi);
        for (int x = grid; x < static_cast<int>(_width); x += grid)
        {
            MoveToEx(dc, x, 0, nullptr);
            LineTo(dc, x, static_cast<int>(_height));
        }
        for (int y = grid; y < static_cast<int>(_height); y += grid)
        {
            MoveToEx(dc, 0, y, nullptr);
            LineTo(dc, static_cast<int>(_width), y);
        }

        const int centerX = static_cast<int>(_width / 2U);
        const int centerY = static_cast<int>(_height / 2U);
        const int minimumDimension = _width < _height ? static_cast<int>(_width) : static_cast<int>(_height);
        const int orbitRadiusX = minimumDimension * 3 / 7;
        const int orbitRadiusY = minimumDimension / 4;

        SelectObject(dc, _orbitPen.get());
        SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Ellipse(dc, centerX - orbitRadiusX, centerY - orbitRadiusY, centerX + orbitRadiusX, centerY + orbitRadiusY);
        Ellipse(dc, centerX - orbitRadiusY, centerY - orbitRadiusX, centerX + orbitRadiusY, centerY + orbitRadiusX);

        const ULONGLONG elapsedMilliseconds = _elapsedBeforePause + (_visible ? GetTickCount64() - _resumeTick : 0ULL);
        const double angle = static_cast<double>(elapsedMilliseconds) * 0.00115;
        HGDIOBJ previousBrush = SelectObject(dc, _dimBrush.get());
        SelectObject(dc, GetStockObject(NULL_PEN));
        for (int trail = 7; trail >= 1; --trail)
        {
            const double trailAngle = angle - static_cast<double>(trail) * 0.13;
            const int x = centerX + static_cast<int>(std::cos(trailAngle) * static_cast<double>(orbitRadiusX));
            const int y = centerY + static_cast<int>(std::sin(trailAngle) * static_cast<double>(orbitRadiusY));
            const int radius = ScaleForDpi(2 + (8 - trail) / 2, _dpi);
            Ellipse(dc, x - radius, y - radius, x + radius, y + radius);
        }

        SelectObject(dc, _redBrush.get());
        const int redX = centerX + static_cast<int>(std::cos(angle) * static_cast<double>(orbitRadiusX));
        const int redY = centerY + static_cast<int>(std::sin(angle) * static_cast<double>(orbitRadiusY));
        const int redRadius = ScaleForDpi(7, _dpi);
        Ellipse(dc, redX - redRadius, redY - redRadius, redX + redRadius, redY + redRadius);

        SelectObject(dc, _cyanBrush.get());
        const double cyanAngle = -angle * 0.72 + kPi * 0.5;
        const int cyanX = centerX + static_cast<int>(std::cos(cyanAngle) * static_cast<double>(orbitRadiusY));
        const int cyanY = centerY + static_cast<int>(std::sin(cyanAngle) * static_cast<double>(orbitRadiusX));
        const int cyanRadius = ScaleForDpi(5, _dpi);
        Ellipse(dc, cyanX - cyanRadius, cyanY - cyanRadius, cyanX + cyanRadius, cyanY + cyanRadius);

        const int cardWidth = ScaleForDpi(112, _dpi);
        const int cardHeight = ScaleForDpi(128, _dpi);
        RECT card{centerX - cardWidth / 2, centerY - cardHeight / 2, centerX + cardWidth / 2, centerY + cardHeight / 2};
        SelectObject(dc, _panelBrush.get());
        SelectObject(dc, _redPen.get());
        RoundRect(dc, card.left, card.top, card.right, card.bottom, ScaleForDpi(10, _dpi), ScaleForDpi(10, _dpi));

        HGDIOBJ previousFont = SelectObject(dc, _detailFont.get());
        SetTextColor(dc, RGB(188, 194, 207));
        RECT numberBounds{card.left + ScaleForDpi(10, _dpi), card.top + ScaleForDpi(7, _dpi), card.right, card.bottom};
        DrawTextW(dc, L"54", -1, &numberBounds, DT_LEFT | DT_TOP | DT_SINGLELINE);

        SelectObject(dc, _symbolFont.get());
        SetTextColor(dc, RGB(232, 50, 61));
        RECT symbolBounds{card.left, card.top + ScaleForDpi(25, _dpi), card.right, card.bottom};
        DrawTextW(dc, L"Xe", -1, &symbolBounds, DT_CENTER | DT_TOP | DT_SINGLELINE);

        SelectObject(dc, _detailFont.get());
        SetTextColor(dc, RGB(219, 222, 230));
        RECT xenonBounds{card.left, card.bottom - ScaleForDpi(29, _dpi), card.right, card.bottom};
        DrawTextW(dc, L"XENON", -1, &xenonBounds, DT_CENTER | DT_TOP | DT_SINGLELINE);

        SelectObject(dc, _titleFont.get());
        SetTextColor(dc, RGB(239, 241, 246));
        RECT titleBounds{ScaleForDpi(18, _dpi), ScaleForDpi(14, _dpi), static_cast<LONG>(_width),
                         ScaleForDpi(42, _dpi)};
        DrawTextW(dc, L"NATIVE WINDOW / GDI", -1, &titleBounds, DT_LEFT | DT_TOP | DT_SINGLELINE);

        SelectObject(dc, _detailFont.get());
        SetTextColor(dc, RGB(114, 124, 145));
        RECT statusBounds{ScaleForDpi(18, _dpi), static_cast<LONG>(_height) - ScaleForDpi(35, _dpi),
                          static_cast<LONG>(_width) - ScaleForDpi(18, _dpi), static_cast<LONG>(_height)};
        DrawTextW(dc, L"DOUBLE BUFFERED  •  30 FPS  •  IDLE WHEN HIDDEN", -1, &statusBounds,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(dc, previousFont);
        SelectObject(dc, previousBrush);
        SelectObject(dc, previousPen);
    }

    std::atomic<ULONG> _references{1};
    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    HWND _container = nullptr;
    wil::unique_hwnd _window;
    wil::unique_hdc _memoryDc;
    wil::unique_hbitmap _surface;
    HGDIOBJ _previousBitmap = nullptr;
    wil::unique_hbrush _backgroundBrush;
    wil::unique_hbrush _panelBrush;
    wil::unique_hbrush _redBrush;
    wil::unique_hbrush _cyanBrush;
    wil::unique_hbrush _dimBrush;
    wil::unique_hpen _gridPen;
    wil::unique_hpen _orbitPen;
    wil::unique_hpen _redPen;
    wil::unique_hfont _titleFont;
    wil::unique_hfont _symbolFont;
    wil::unique_hfont _detailFont;
    std::uint32_t _width = 0;
    std::uint32_t _height = 0;
    UINT _dpi = 0;
    ULONGLONG _elapsedBeforePause = 0;
    ULONGLONG _resumeTick = 0;
    bool _timerRunning = false;
    bool _visible = false;

    friend HRESULT EnsureWindowClass() noexcept;
};

[[nodiscard]] HRESULT EnsureWindowClass() noexcept
{
    if (!g_moduleInstance)
    {
        return E_UNEXPECTED;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = GdiOrbitWidget::WindowProcedure;
    windowClass.hInstance = g_moduleInstance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass))
    {
        return S_OK;
    }

    const DWORD error = GetLastError();
    return error == ERROR_CLASS_ALREADY_EXISTS ? S_OK : HRESULT_FROM_WIN32(error);
}

class GdiOrbitProvider final : public IRedXeWidgetProvider
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

        auto* created = new (std::nothrow) GdiOrbitWidget(std::move(providerOwner));
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    std::atomic<ULONG> _references{1};
};

HRESULT CreateGdiOrbitProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost*,
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

    auto* provider = new (std::nothrow) GdiOrbitProvider();
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = provider;
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateGdiOrbitProvider},
};
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) noexcept
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_moduleInstance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

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
