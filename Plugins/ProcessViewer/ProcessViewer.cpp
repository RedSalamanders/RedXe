#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/Data.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"
#include "ProcessViewerTestContract.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <new>
#include <string_view>
#include <strsafe.h>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.process-viewer";
constexpr char kWidgetTypeId[] = "process-viewer";
constexpr char kSystemDataPluginId[] = "builtin.system-data";
constexpr char kProcessDataSetId[] = "process.list";
constexpr char kSettingsSchema[] =
    R"json({"type":"object","additionalProperties":false,"required":["topN"],"properties":{"topN":{"type":"integer","minimum":1,"maximum":32}}})json";
constexpr char kSettingsDefaults[] = R"json({"topN":10})json";
constexpr wchar_t kWindowClassName[] = L"RedXe.Plugin.ProcessViewer";
constexpr UINT kDataChangedMessage = WM_APP + 0x241;
constexpr uint32_t kMaximumRows = 32;
constexpr uint32_t kMaximumNameCharacters = 95;
constexpr uint32_t kDefaultTopN = 10;
constexpr uint32_t kSampleIntervalMilliseconds = 2000;

HINSTANCE g_moduleInstance = nullptr;
std::atomic<uint32_t> g_liveProviderCount = 0;
std::atomic<uint32_t> g_liveWidgetCount = 0;
std::atomic<uint32_t> g_liveSubscriptionCount = 0;
std::atomic<uint32_t> g_sampleCount = 0;
std::atomic<uint32_t> g_paintCount = 0;
std::atomic<uint32_t> g_lastPublishedRowCount = 0;
std::atomic<uint32_t> g_configuredTopN = 0;

constexpr RedXePluginSettingsContract kSettingsContract{
    sizeof(RedXePluginSettingsContract), kSettingsSchema, sizeof(kSettingsSchema) - 1, kSettingsDefaults,
    sizeof(kSettingsDefaults) - 1,
};

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"Process Viewer",
        L"Displays the highest-CPU local processes from the RedXe system-data provider.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kWidgetTypeId,
        L"Process Viewer",
        L"Top processes ranked by total machine CPU share, with memory and PID details.",
        1280.0f,
        720.0f,
        480.0f,
        260.0f,
        RedXeWidgetFlagNone,
    },
};

struct ProcessViewerConfiguration final
{
    uint32_t topN = kDefaultTopN;
};

[[nodiscard]] HRESULT ReadConfiguration(const RedXeFactoryOptions* options,
                                        ProcessViewerConfiguration& configuration) noexcept
{
    configuration = ProcessViewerConfiguration{};
    if (!options)
    {
        return S_OK;
    }
    if (options->sizeBytes != sizeof(RedXeFactoryOptions))
    {
        return E_INVALIDARG;
    }
    if (!options->configurationJsonUtf8 && options->configurationBytes == 0)
    {
        return S_OK;
    }
    if (!options->configurationJsonUtf8 || options->configurationBytes == 0 ||
        options->configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
    {
        return E_INVALIDARG;
    }

    constexpr std::string_view prefix = R"json({"plugin":{},"instance":{"topN":)json";
    constexpr std::string_view suffix = "}}";
    const std::string_view json(options->configurationJsonUtf8, options->configurationBytes);
    if (!json.starts_with(prefix) || !json.ends_with(suffix) || json.size() <= prefix.size() + suffix.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    const std::string_view number = json.substr(prefix.size(), json.size() - prefix.size() - suffix.size());
    uint32_t parsed = 0;
    const auto result = std::from_chars(number.data(), number.data() + number.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != number.data() + number.size() || parsed == 0 || parsed > kMaximumRows)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    configuration.topN = parsed;
    return S_OK;
}

[[nodiscard]] int ScaleForDpi(int value, UINT dpi) noexcept
{
    return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] HRESULT LastErrorOrFailure() noexcept
{
    const DWORD error = GetLastError();
    return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

struct ProcessDisplayRow final
{
    std::array<wchar_t, kMaximumNameCharacters + 1> name{};
    uint32_t nameCharacters = 0;
    uint32_t processId = 0;
    double cpuPercent = 0.0;
    uint64_t workingSetBytes = 0;
    uint32_t threadCount = 0;
    bool cpuAvailable = false;
};

[[nodiscard]] bool RanksBefore(const ProcessDisplayRow& left, const ProcessDisplayRow& right) noexcept
{
    if (left.cpuAvailable != right.cpuAvailable)
    {
        return left.cpuAvailable;
    }
    if (left.cpuPercent != right.cpuPercent)
    {
        return left.cpuPercent > right.cpuPercent;
    }
    if (left.workingSetBytes != right.workingSetBytes)
    {
        return left.workingSetBytes > right.workingSetBytes;
    }
    return left.processId < right.processId;
}

class ProcessSnapshotCache final
{
  public:
    explicit ProcessSnapshotCache(uint32_t topN) noexcept : _topN(topN) {}

    void SetWindow(HWND window) noexcept
    {
        _window.store(window, std::memory_order_release);
    }

    [[nodiscard]] HRESULT Publish(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!snapshot)
        {
            return E_POINTER;
        }
        if (snapshot->sizeBytes != sizeof(RedXeDataSnapshot) || !snapshot->dataSetId ||
            !RedXeAsciiEqualsIgnoreCase(snapshot->dataSetId, kProcessDataSetId) || snapshot->columnCount < 7 ||
            snapshot->rowCount > 2048 || (snapshot->rowCount != 0 && !snapshot->rows))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        std::array<ProcessDisplayRow, kMaximumRows> selected{};
        uint32_t selectedCount = 0;
        for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
        {
            const RedXeDataRow& sourceRow = snapshot->rows[rowIndex];
            if (sourceRow.sizeBytes != sizeof(RedXeDataRow) || !sourceRow.values || sourceRow.valueCount < 7)
            {
                continue;
            }
            const RedXeDataValue* values = sourceRow.values;
            bool valueSizesValid = true;
            for (uint32_t valueIndex = 0; valueIndex < 7; ++valueIndex)
            {
                valueSizesValid = valueSizesValid && values[valueIndex].sizeBytes == sizeof(RedXeDataValue);
            }
            if (!valueSizesValid || values[0].valueType != RedXeDataValueTypeUInt64 ||
                values[1].valueType != RedXeDataValueTypeUtf16 || values[2].valueType != RedXeDataValueTypeFloat64 ||
                values[3].valueType != RedXeDataValueTypeUInt64 || values[4].valueType != RedXeDataValueTypeUInt64 ||
                values[5].valueType != RedXeDataValueTypeUInt64 || values[6].valueType != RedXeDataValueTypeUInt64 ||
                values[0].uint64Value > UINT32_MAX || !std::isfinite(values[2].float64Value))
            {
                continue;
            }

            ProcessDisplayRow candidate{};
            candidate.processId = static_cast<uint32_t>(values[0].uint64Value);
            candidate.cpuAvailable = values[2].quality == RedXeDataQualityGood;
            candidate.cpuPercent = candidate.cpuAvailable ? std::clamp(values[2].float64Value, 0.0, 100.0) : 0.0;
            candidate.workingSetBytes = values[3].quality == RedXeDataQualityGood ? values[3].uint64Value : 0;
            candidate.threadCount = values[5].quality == RedXeDataQualityGood
                                        ? static_cast<uint32_t>(
                                              std::min(values[5].uint64Value, static_cast<uint64_t>(UINT32_MAX)))
                                        : 0;
            if (values[1].quality == RedXeDataQualityGood && values[1].utf16Value)
            {
                candidate.nameCharacters =
                    std::min(values[1].utf16Characters, static_cast<uint32_t>(kMaximumNameCharacters));
                std::wmemcpy(candidate.name.data(), values[1].utf16Value, candidate.nameCharacters);
                candidate.name[candidate.nameCharacters] = L'\0';
            }
            if (candidate.nameCharacters == 0)
            {
                constexpr wchar_t unnamed[] = L"(unnamed)";
                candidate.nameCharacters = static_cast<uint32_t>(std::size(unnamed) - 1);
                std::wmemcpy(candidate.name.data(), unnamed, std::size(unnamed));
            }

            if (selectedCount < _topN)
            {
                selected[selectedCount++] = candidate;
            }
            else if (!RanksBefore(candidate, selected[selectedCount - 1]))
            {
                continue;
            }
            else
            {
                selected[selectedCount - 1] = candidate;
            }
            for (uint32_t index = selectedCount - 1;
                 index > 0 && RanksBefore(selected[index], selected[index - 1]); --index)
            {
                std::swap(selected[index], selected[index - 1]);
            }
        }

        AcquireSRWLockExclusive(&_lock);
        _rows = selected;
        _rowCount = selectedCount;
        _sequence = snapshot->sequence;
        _truncated = (snapshot->flags & RedXeDataSnapshotFlagTruncated) != 0;
        ReleaseSRWLockExclusive(&_lock);

        g_sampleCount.fetch_add(1, std::memory_order_relaxed);
        g_lastPublishedRowCount.store(selectedCount, std::memory_order_relaxed);
        const HWND window = _window.load(std::memory_order_acquire);
        if (window)
        {
            (void)PostMessageW(window, kDataChangedMessage, 0, 0);
        }
        return S_OK;
    }

    [[nodiscard]] uint32_t Copy(std::array<ProcessDisplayRow, kMaximumRows>& rows, uint64_t& sequence,
                                     bool& truncated) const noexcept
    {
        AcquireSRWLockShared(&_lock);
        rows = _rows;
        const uint32_t count = _rowCount;
        sequence = _sequence;
        truncated = _truncated;
        ReleaseSRWLockShared(&_lock);
        return count;
    }

  private:
    mutable SRWLOCK _lock = SRWLOCK_INIT;
    std::array<ProcessDisplayRow, kMaximumRows> _rows{};
    std::atomic<HWND> _window = nullptr;
    uint64_t _sequence = 0;
    uint32_t _rowCount = 0;
    uint32_t _topN;
    bool _truncated = false;
};

class ProcessDataSink final : public IRedXeDataSink
{
  public:
    explicit ProcessDataSink(ProcessSnapshotCache& cache) noexcept : _cache(&cache) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId != __uuidof(IUnknown) && interfaceId != __uuidof(IRedXeDataSink))
        {
            return E_NOINTERFACE;
        }
        *result = static_cast<IRedXeDataSink*>(this);
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

    HRESULT STDMETHODCALLTYPE OnDataSnapshot(const RedXeDataSnapshot* snapshot) noexcept override
    {
        return _cache->Publish(snapshot);
    }

  private:
    std::atomic<ULONG> _references{1};
    ProcessSnapshotCache* _cache;
};

[[nodiscard]] HRESULT EnsureWindowClass() noexcept;

class ProcessViewerWidget final : public IRedXeWidget, public IRedXeWindowWidget
{
  public:
    ProcessViewerWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner, uint32_t topN) noexcept
        : _providerOwner(std::move(providerOwner)), _cache(topN), _topN(topN)
    {
        g_liveWidgetCount.fetch_add(1, std::memory_order_relaxed);
        g_configuredTopN.store(topN, std::memory_order_relaxed);
    }

    ~ProcessViewerWidget()
    {
        Detach();
        if (_subscription)
        {
            _subscription.reset();
            g_liveSubscriptionCount.fetch_sub(1, std::memory_order_relaxed);
        }
        _sink.reset();
        g_liveWidgetCount.fetch_sub(1, std::memory_order_relaxed);
    }

    [[nodiscard]] HRESULT InitializeSubscription(IRedXeDataProvider& provider) noexcept
    {
        auto* sink = new (std::nothrow) ProcessDataSink(_cache);
        if (!sink)
        {
            return E_OUTOFMEMORY;
        }
        _sink.attach(sink);
        const RedXeDataSubscriptionOptions options{
            sizeof(RedXeDataSubscriptionOptions),
            kProcessDataSetId,
            kSampleIntervalMilliseconds,
        };
        const HRESULT result = provider.Subscribe(&options, _sink.get(), _subscription.put());
        if (FAILED(result))
        {
            _sink.reset();
            return result;
        }
        g_liveSubscriptionCount.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
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
        if (context->sizeBytes != sizeof(RedXeWindowWidgetAttachContext) || !context->container ||
            context->widthPixels == 0 || context->heightPixels == 0 || context->dpi == 0 ||
            !IsWindow(context->container) || _window)
        {
            return E_INVALIDARG;
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
            _cache.SetWindow(nullptr);
            _window.reset();
            _container = nullptr;
            return result;
        }
        _cache.SetWindow(window);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Resize(const RedXeWindowWidgetSizeContext* context) noexcept override
    {
        if (!context)
        {
            return E_POINTER;
        }
        if (context->sizeBytes != sizeof(RedXeWindowWidgetSizeContext) || context->widthPixels == 0 ||
            context->heightPixels == 0 || context->dpi == 0 || !_window)
        {
            return E_INVALIDARG;
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
        const bool shouldShow = visible != FALSE;
        if (_visible == shouldShow)
        {
            return S_OK;
        }
        if (!_window || !_subscription)
        {
            return E_UNEXPECTED;
        }
        if (shouldShow)
        {
            const HRESULT result = _subscription->SetActive(TRUE);
            if (FAILED(result))
            {
                return result;
            }
            _visible = true;
            ShowWindow(_window.get(), SW_SHOWNA);
            InvalidateRect(_window.get(), nullptr, FALSE);
        }
        else
        {
            const HRESULT result = _subscription->SetActive(FALSE);
            if (FAILED(result))
            {
                return result;
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
        _cache.SetWindow(nullptr);
        _window.reset();
        ReleaseDrawingResources();
        _container = nullptr;
    }

  private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
        auto* widget = reinterpret_cast<ProcessViewerWidget*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            widget = static_cast<ProcessViewerWidget*>(create->lpCreateParams);
            widget->_window.reset(window);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(widget));
        }
        if (!widget)
        {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        switch (message)
        {
        case kDataChangedMessage:
            if (widget->_visible)
            {
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
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
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    [[nodiscard]] HRESULT RebuildDrawingResources(uint32_t width, uint32_t height, UINT dpi) noexcept
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
            _backgroundBrush.reset(CreateSolidBrush(RGB(7, 10, 16)));
            _headerBrush.reset(CreateSolidBrush(RGB(17, 23, 34)));
            _rowBrush.reset(CreateSolidBrush(RGB(11, 16, 25)));
            _alternateRowBrush.reset(CreateSolidBrush(RGB(14, 20, 31)));
            _separatorPen.reset(CreatePen(PS_SOLID, 1, RGB(36, 47, 64)));
            _titleFont.reset(CreateFontW(-ScaleForDpi(16, dpi), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI"));
            _headerFont.reset(CreateFontW(-ScaleForDpi(10, dpi), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                          DEFAULT_PITCH | FF_SWISS, L"Segoe UI"));
            _rowFont.reset(CreateFontW(-ScaleForDpi(11, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_SWISS, L"Segoe UI"));
            if (!_backgroundBrush || !_headerBrush || !_rowBrush || !_alternateRowBrush || !_separatorPen ||
                !_titleFont || !_headerFont || !_rowFont)
            {
                return LastErrorOrFailure();
            }
            _dpi = dpi;
        }
        return S_OK;
    }

    void ReleaseDrawingResources() noexcept
    {
        _rowFont.reset();
        _headerFont.reset();
        _titleFont.reset();
        _separatorPen.reset();
        _alternateRowBrush.reset();
        _rowBrush.reset();
        _headerBrush.reset();
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
        g_paintCount.fetch_add(1, std::memory_order_relaxed);
    }

    void DrawScene(HDC dc) noexcept
    {
        std::array<ProcessDisplayRow, kMaximumRows> rows{};
        uint64_t sequence = 0;
        bool truncated = false;
        const uint32_t rowCount = _cache.Copy(rows, sequence, truncated);
        const int padding = ScaleForDpi(16, _dpi);
        const int titleHeight = ScaleForDpi(42, _dpi);
        const int headerHeight = ScaleForDpi(26, _dpi);
        const int minimumRowHeight = ScaleForDpi(21, _dpi);
        const int availableHeight = std::max(0, static_cast<int>(_height) - titleHeight - headerHeight - padding);
        const uint32_t visibleRows = std::min(
            rowCount, static_cast<uint32_t>(minimumRowHeight > 0 ? availableHeight / minimumRowHeight : 0));

        const RECT bounds{0, 0, static_cast<LONG>(_width), static_cast<LONG>(_height)};
        FillRect(dc, &bounds, _backgroundBrush.get());
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ previousFont = SelectObject(dc, _titleFont.get());
        HGDIOBJ previousPen = SelectObject(dc, _separatorPen.get());

        SetTextColor(dc, RGB(235, 241, 247));
        RECT title{padding, ScaleForDpi(9, _dpi), static_cast<LONG>(_width) - padding, titleHeight};
        DrawTextW(dc, L"TOP PROCESSES / CPU", -1, &title, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        wchar_t status[96]{};
        if (sequence == 0)
        {
            StringCchCopyW(status, std::size(status), L"Waiting for system data…");
        }
        else
        {
            StringCchPrintfW(status, std::size(status), truncated ? L"Top %u  •  source truncated" : L"Top %u", _topN);
        }
        SelectObject(dc, _headerFont.get());
        SetTextColor(dc, RGB(105, 126, 148));
        RECT statusBounds{static_cast<LONG>(_width) / 2, ScaleForDpi(13, _dpi), static_cast<LONG>(_width) - padding,
                          titleHeight};
        DrawTextW(dc, status, -1, &statusBounds, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT header{padding, titleHeight, static_cast<LONG>(_width) - padding, titleHeight + headerHeight};
        FillRect(dc, &header, _headerBrush.get());
        SetTextColor(dc, RGB(129, 151, 173));
        DrawColumnText(dc, L"#", 0, 8, header, DT_LEFT);
        DrawColumnText(dc, L"PROCESS", 8, 48, header, DT_LEFT);
        DrawColumnText(dc, L"CPU", 48, 60, header, DT_RIGHT);
        DrawColumnText(dc, L"MEMORY", 60, 76, header, DT_RIGHT);
        DrawColumnText(dc, L"THREADS", 76, 88, header, DT_RIGHT);
        DrawColumnText(dc, L"PID", 88, 100, header, DT_RIGHT);

        SelectObject(dc, _rowFont.get());
        const int rowHeight = minimumRowHeight;
        for (uint32_t index = 0; index < visibleRows; ++index)
        {
            RECT rowBounds{padding, header.bottom + static_cast<LONG>(index) * rowHeight,
                           static_cast<LONG>(_width) - padding,
                           header.bottom + static_cast<LONG>(index + 1) * rowHeight};
            FillRect(dc, &rowBounds, (index & 1U) != 0 ? _alternateRowBrush.get() : _rowBrush.get());

            wchar_t rank[12]{};
            wchar_t cpu[24]{};
            wchar_t memory[32]{};
            wchar_t threads[16]{};
            wchar_t processId[24]{};
            StringCchPrintfW(rank, std::size(rank), L"%u", index + 1);
            if (rows[index].cpuAvailable)
            {
                StringCchPrintfW(cpu, std::size(cpu), L"%.1f%%", rows[index].cpuPercent);
            }
            else
            {
                StringCchCopyW(cpu, std::size(cpu), L"—");
            }
            StringCchPrintfW(memory, std::size(memory), L"%llu MB",
                             static_cast<unsigned long long>(rows[index].workingSetBytes / (1024U * 1024U)));
            StringCchPrintfW(threads, std::size(threads), L"%u", rows[index].threadCount);
            StringCchPrintfW(processId, std::size(processId), L"%u", rows[index].processId);

            SetTextColor(dc, RGB(83, 191, 228));
            DrawColumnText(dc, rank, 0, 8, rowBounds, DT_LEFT);
            SetTextColor(dc, RGB(222, 230, 238));
            DrawColumnText(dc, rows[index].name.data(), 8, 48, rowBounds, DT_LEFT);
            SetTextColor(dc, rows[index].cpuAvailable ? RGB(83, 191, 228) : RGB(100, 113, 127));
            DrawColumnText(dc, cpu, 48, 60, rowBounds, DT_RIGHT);
            SetTextColor(dc, RGB(174, 186, 198));
            DrawColumnText(dc, memory, 60, 76, rowBounds, DT_RIGHT);
            DrawColumnText(dc, threads, 76, 88, rowBounds, DT_RIGHT);
            DrawColumnText(dc, processId, 88, 100, rowBounds, DT_RIGHT);
            MoveToEx(dc, rowBounds.left, rowBounds.bottom - 1, nullptr);
            LineTo(dc, rowBounds.right, rowBounds.bottom - 1);
        }
        SelectObject(dc, previousPen);
        SelectObject(dc, previousFont);
    }

    static void DrawColumnText(HDC dc, const wchar_t* text, int leftPercent, int rightPercent, const RECT& row,
                               UINT alignment) noexcept
    {
        const LONG width = row.right - row.left;
        const int inset = 6;
        RECT cell{row.left + width * leftPercent / 100 + inset, row.top, row.left + width * rightPercent / 100 - inset,
                  row.bottom};
        DrawTextW(dc, text, -1, &cell, alignment | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    std::atomic<ULONG> _references{1};
    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    ProcessSnapshotCache _cache;
    wil::com_ptr_nothrow<IRedXeDataSink> _sink;
    wil::com_ptr_nothrow<IRedXeDataSubscription> _subscription;
    HWND _container = nullptr;
    wil::unique_hwnd _window;
    wil::unique_hdc _memoryDc;
    wil::unique_hbitmap _surface;
    HGDIOBJ _previousBitmap = nullptr;
    wil::unique_hbrush _backgroundBrush;
    wil::unique_hbrush _headerBrush;
    wil::unique_hbrush _rowBrush;
    wil::unique_hbrush _alternateRowBrush;
    wil::unique_hpen _separatorPen;
    wil::unique_hfont _titleFont;
    wil::unique_hfont _headerFont;
    wil::unique_hfont _rowFont;
    uint32_t _width = 0;
    uint32_t _height = 0;
    UINT _dpi = 0;
    uint32_t _topN;
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
    windowClass.lpfnWndProc = ProcessViewerWidget::WindowProcedure;
    windowClass.hInstance = g_moduleInstance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        return S_OK;
    }
    return HRESULT_FROM_WIN32(GetLastError());
}

class ProcessViewerProvider final : public IRedXeWidgetProvider
{
  public:
    ProcessViewerProvider(wil::com_ptr_nothrow<IRedXeDataProvider>&& dataProvider, uint32_t topN) noexcept
        : _dataProvider(std::move(dataProvider)), _topN(topN)
    {
        g_liveProviderCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~ProcessViewerProvider()
    {
        g_liveProviderCount.fetch_sub(1, std::memory_order_relaxed);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId != __uuidof(IUnknown) && interfaceId != __uuidof(IRedXeWidgetProvider))
        {
            return E_NOINTERFACE;
        }
        *result = static_cast<IRedXeWidgetProvider*>(this);
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
        auto* created = new (std::nothrow) ProcessViewerWidget(std::move(providerOwner), _topN);
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        result = created->InitializeSubscription(*_dataProvider);
        if (FAILED(result))
        {
            delete created;
            return result;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    std::atomic<ULONG> _references{1};
    wil::com_ptr_nothrow<IRedXeDataProvider> _dataProvider;
    uint32_t _topN;
};

HRESULT CreateProcessViewerProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                    void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
    }
    if (!host)
    {
        return E_POINTER;
    }
    ProcessViewerConfiguration configuration{};
    HRESULT configurationResult = ReadConfiguration(options, configuration);
    if (FAILED(configurationResult))
    {
        return configurationResult;
    }
    wil::com_ptr_nothrow<IRedXeDataProvider> dataProvider;
    configurationResult = host->GetDataProvider(kSystemDataPluginId, dataProvider.put());
    if (FAILED(configurationResult))
    {
        return configurationResult;
    }
    auto* provider = new (std::nothrow) ProcessViewerProvider(std::move(dataProvider), configuration.topN);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = static_cast<IRedXeWidgetProvider*>(provider);
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateProcessViewerProvider},
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
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<uint32_t>(kMetadata.size()), metadata,
                                         count);
}

extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* pluginId,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetStaticPluginSettingsContract(kPluginId, pluginId, &kSettingsContract, contract);
}

extern "C" HRESULT __stdcall RedXeProcessViewerGetTestDiagnostics(ProcessViewerTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(ProcessViewerTestDiagnostics))
    {
        return E_INVALIDARG;
    }
    diagnostics->liveProviderCount = g_liveProviderCount.load(std::memory_order_relaxed);
    diagnostics->liveWidgetCount = g_liveWidgetCount.load(std::memory_order_relaxed);
    diagnostics->liveSubscriptionCount = g_liveSubscriptionCount.load(std::memory_order_relaxed);
    diagnostics->sampleCount = g_sampleCount.load(std::memory_order_relaxed);
    diagnostics->paintCount = g_paintCount.load(std::memory_order_relaxed);
    diagnostics->lastPublishedRowCount = g_lastPublishedRowCount.load(std::memory_order_relaxed);
    diagnostics->configuredTopN = g_configuredTopN.load(std::memory_order_relaxed);
    return S_OK;
}
