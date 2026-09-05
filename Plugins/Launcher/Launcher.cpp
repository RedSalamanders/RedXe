#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Widget.h"

#include "LauncherPixelShader.h"
#include "LauncherTestContract.h"
#include "LauncherVertexShader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

#include <commctrl.h>
#include <commoncontrols.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

#include <yyjson.h>

namespace
{
constexpr char kPluginId[] = "builtin.launcher";
constexpr char kWidgetTypeId[] = "launcher";
constexpr char kSettingsSchema[] =
    R"json({"type":"object","additionalProperties":false,"properties":{"shortcuts":{"type":"array","minItems":0,"maxItems":8,"items":{"type":"object","additionalProperties":false,"properties":{"target":{"type":"string"},"iconPng":{"type":"string"}},"required":["target"]}}}})json";
constexpr char kSettingsDefaults[] = R"json({"shortcuts":[]})json";
constexpr RedXePluginSettingsContract kSettingsContract{
    sizeof(RedXePluginSettingsContract), kSettingsSchema, sizeof(kSettingsSchema) - 1, kSettingsDefaults,
    sizeof(kSettingsDefaults) - 1,
};

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"Launcher",
        L"Large-icon shortcut grid with jumbo extraction, drop-to-add, and shell launch.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kWidgetTypeId,
        L"Launcher",
        L"A Direct3D shortcut launcher with jumbo icons and a 3D launch motion.",
        480.0f,
        480.0f,
        160.0f,
        160.0f,
        RedXeWidgetFlagNone,
    },
};

constexpr uint32_t kMaximumShortcuts = 8;
constexpr uint32_t kIconEdge = 256;
constexpr uint32_t kTargetCapacity = 513;
constexpr uint32_t kIconPngCapacity = 261;
constexpr uint32_t kInstanceIdCapacity = 64;
constexpr float kLaunchDurationSeconds = 0.4f;

using unique_yyjson_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using unique_yyjson_mut_doc = wil::unique_any<yyjson_mut_doc*, decltype(&yyjson_mut_doc_free), yyjson_mut_doc_free>;
using unique_malloc_string = wil::unique_any<char*, decltype(&free), free>;
constexpr uint32_t kSettingsJsonCapacity = 4096;

enum class PinMode : uint32_t
{
    Live = 0,
    None,
    Override,
};

std::atomic<uint32_t> gLiveWidgets{0};
std::atomic<uint64_t> gExtractCalls{0};
std::atomic<uint64_t> gTextureUploads{0};
std::atomic<uint64_t> gDrawCalls{0};
std::atomic<uint64_t> gLaunchCount{0};
std::atomic<uint64_t> gShellExecuteCount{0};
std::atomic<uint32_t> gLastLaunchKind{0};
std::atomic<uint32_t> gLastShellMask{0};
std::atomic<uint32_t> gLastVerbWasNull{1};
std::atomic<uint32_t> gLastShow{0};
std::atomic<uint32_t> gLargestIconEdge{0};
std::atomic<uint32_t> gAuthoredCount{0};
std::atomic<uint32_t> gDisplayCount{0};
std::atomic<uint32_t> gUsingPins{0};
std::atomic<uint32_t> gColumns{0};
std::atomic<uint32_t> gRows{0};
std::atomic<uint32_t> gLastInstanceCount{0};
std::atomic<uint32_t> gLastDrawCount{0};
SRWLOCK gPinLock = SRWLOCK_INIT;
PinMode gPinMode = PinMode::Live;
wchar_t gPinOverride[1024]{};

[[nodiscard]] bool IsAutomatedHost() noexcept
{
    std::array<wchar_t, 8> value{};
    const DWORD length =
        GetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", value.data(), static_cast<DWORD>(value.size()));
    return length == 1 && value[0] == L'1';
}

[[nodiscard]] int ClassifyTarget(std::string_view target) noexcept
{
    if (target.empty() || target.size() > 512)
    {
        return 0;
    }
    if (target.size() >= 3 && ((target[0] >= 'A' && target[0] <= 'Z') || (target[0] >= 'a' && target[0] <= 'z')) &&
        target[1] == ':' && (target[2] == '\\' || target[2] == '/'))
    {
        return 1;
    }
    if (target.size() >= 2 && target[0] == '\\' && target[1] == '\\')
    {
        return 1;
    }
    if (target.size() < 3 || !std::isalpha(static_cast<unsigned char>(target[0])))
    {
        return 0;
    }
    size_t index = 1;
    while (index < target.size())
    {
        const unsigned char value = static_cast<unsigned char>(target[index]);
        if (!(std::isalnum(value) || value == '+' || value == '.' || value == '-'))
        {
            break;
        }
        ++index;
    }
    return (index >= 2 && index < target.size() && target[index] == ':') ? 2 : 0;
}

bool CopyNarrow(std::string_view source, char* destination, size_t capacity) noexcept
{
    if (!destination || capacity == 0 || source.size() + 1 > capacity)
    {
        return false;
    }
    std::memcpy(destination, source.data(), source.size());
    destination[source.size()] = '\0';
    return true;
}

bool CopyWide(const wchar_t* source, wchar_t* destination, size_t capacity) noexcept
{
    if (!source || !destination || capacity == 0)
    {
        return false;
    }
    const size_t length = std::wcslen(source);
    if (length + 1 > capacity)
    {
        return false;
    }
    std::memcpy(destination, source, (length + 1) * sizeof(wchar_t));
    return true;
}

[[nodiscard]] bool Utf8ToWide(std::string_view utf8, wchar_t* wide, size_t capacity) noexcept
{
    if (!wide || capacity == 0)
    {
        return false;
    }
    if (utf8.empty())
    {
        wide[0] = L'\0';
        return true;
    }
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                                            wide, static_cast<int>(capacity - 1));
    if (written <= 0)
    {
        return false;
    }
    wide[written] = L'\0';
    return true;
}

[[nodiscard]] bool WideToUtf8(const wchar_t* wide, char* utf8, size_t capacity) noexcept
{
    if (!wide || !utf8 || capacity == 0)
    {
        return false;
    }
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, utf8, static_cast<int>(capacity),
                                            nullptr, nullptr);
    return written > 0;
}

[[nodiscard]] bool EndsWithInsensitive(const wchar_t* name, const wchar_t* suffix) noexcept
{
    const size_t nameLength = std::wcslen(name);
    const size_t suffixLength = std::wcslen(suffix);
    if (nameLength < suffixLength)
    {
        return false;
    }
    return CompareStringOrdinal(name + nameLength - suffixLength, static_cast<int>(suffixLength), suffix,
                                static_cast<int>(suffixLength), TRUE) == CSTR_EQUAL;
}

struct ShortcutRecord final
{
    wchar_t target[kTargetCapacity]{};
    wchar_t iconPng[kIconPngCapacity]{};
    char targetUtf8[kTargetCapacity]{};
    int kind = 0;
    std::unique_ptr<uint8_t[]> bgra;
    uint32_t sourceEdge = 0;
};

struct LauncherConfiguration final
{
    uint32_t count = 0;
    std::array<ShortcutRecord, kMaximumShortcuts> items{};
};

struct alignas(16) LauncherConstants final
{
    float viewportWidth;
    float viewportHeight;
    float drawMode;
    float hint;
    float background[4];
    float hintColor[4];
    float iconRect[kMaximumShortcuts][4];
    float iconMotion[kMaximumShortcuts][4];
    uint32_t iconCount;
    uint32_t pad[3];
};

static_assert(sizeof(LauncherConstants) == 320);

[[nodiscard]] HRESULT CopyWicToBgra(IWICBitmapSource* source, ShortcutRecord& record) noexcept
{
    if (!source)
    {
        return E_POINTER;
    }
    UINT width = 0;
    UINT height = 0;
    HRESULT result = source->GetSize(&width, &height);
    if (FAILED(result) || width == 0 || height == 0)
    {
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    record.sourceEdge = std::max(width, height);
    if (record.sourceEdge > gLargestIconEdge.load(std::memory_order_relaxed))
    {
        gLargestIconEdge.store(record.sourceEdge, std::memory_order_relaxed);
    }

    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    result = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICBitmapSource> converted = source;
    if (width != kIconEdge || height != kIconEdge)
    {
        wil::com_ptr_nothrow<IWICBitmapScaler> scaler;
        result = factory->CreateBitmapScaler(scaler.put());
        if (FAILED(result))
        {
            return result;
        }
        result = scaler->Initialize(source, kIconEdge, kIconEdge, WICBitmapInterpolationModeHighQualityCubic);
        if (FAILED(result))
        {
            return result;
        }
        converted = scaler;
        width = kIconEdge;
        height = kIconEdge;
    }
    wil::com_ptr_nothrow<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(converter.put());
    if (FAILED(result))
    {
        return result;
    }
    result = converter->Initialize(converted.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                   0.0, WICBitmapPaletteTypeMedianCut);
    if (FAILED(result))
    {
        return result;
    }
    record.bgra.reset(new (std::nothrow) uint8_t[kIconEdge * kIconEdge * 4]);
    if (!record.bgra)
    {
        return E_OUTOFMEMORY;
    }
    const UINT stride = kIconEdge * 4;
    return converter->CopyPixels(nullptr, stride, stride * kIconEdge, record.bgra.get());
}

[[nodiscard]] HRESULT TrimAndFit(ShortcutRecord& record) noexcept
{
    if (!record.bgra)
    {
        return E_UNEXPECTED;
    }
    uint32_t minX = kIconEdge;
    uint32_t minY = kIconEdge;
    uint32_t maxX = 0;
    uint32_t maxY = 0;
    for (uint32_t y = 0; y < kIconEdge; ++y)
    {
        for (uint32_t x = 0; x < kIconEdge; ++x)
        {
            const uint8_t alpha = record.bgra[(y * kIconEdge + x) * 4 + 3];
            if (alpha > 8)
            {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (minX > maxX || minY > maxY)
    {
        return S_OK;
    }
    const uint32_t trimmedWidth = maxX - minX + 1;
    const uint32_t trimmedHeight = maxY - minY + 1;
    if (trimmedWidth >= kIconEdge - 4 && trimmedHeight >= kIconEdge - 4)
    {
        return S_OK;
    }
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result =
        CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICBitmap> bitmap;
    result = factory->CreateBitmapFromMemory(kIconEdge, kIconEdge, GUID_WICPixelFormat32bppPBGRA, kIconEdge * 4,
                                             kIconEdge * kIconEdge * 4, record.bgra.get(), bitmap.put());
    if (FAILED(result))
    {
        return result;
    }
    WICRect crop{static_cast<INT>(minX), static_cast<INT>(minY), static_cast<INT>(trimmedWidth),
                 static_cast<INT>(trimmedHeight)};
    wil::com_ptr_nothrow<IWICBitmapClipper> clipper;
    result = factory->CreateBitmapClipper(clipper.put());
    if (FAILED(result))
    {
        return result;
    }
    result = clipper->Initialize(bitmap.get(), &crop);
    if (FAILED(result))
    {
        return result;
    }
    return CopyWicToBgra(clipper.get(), record);
}

[[nodiscard]] HRESULT ExtractFromPng(const wchar_t* path, ShortcutRecord& record) noexcept
{
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result =
        CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICBitmapDecoder> decoder;
    result =
        factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(result))
    {
        return result;
    }
    GUID container{};
    result = decoder->GetContainerFormat(&container);
    if (FAILED(result) || container != GUID_ContainerFormatPng)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    wil::com_ptr_nothrow<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, frame.put());
    if (FAILED(result))
    {
        return result;
    }
    result = CopyWicToBgra(frame.get(), record);
    return SUCCEEDED(result) ? TrimAndFit(record) : result;
}

[[nodiscard]] HRESULT ExtractFromHicon(HICON icon, ShortcutRecord& record) noexcept
{
    if (!icon)
    {
        return E_POINTER;
    }
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result =
        CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICBitmap> bitmap;
    result = factory->CreateBitmapFromHICON(icon, bitmap.put());
    if (FAILED(result))
    {
        return result;
    }
    result = CopyWicToBgra(bitmap.get(), record);
    return SUCCEEDED(result) ? TrimAndFit(record) : result;
}

[[nodiscard]] HRESULT ExtractFromHbitmap(HBITMAP bitmap, ShortcutRecord& record) noexcept
{
    if (!bitmap)
    {
        return E_POINTER;
    }
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result =
        CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICBitmap> wic;
    result = factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUsePremultipliedAlpha, wic.put());
    if (FAILED(result))
    {
        return result;
    }
    result = CopyWicToBgra(wic.get(), record);
    return SUCCEEDED(result) ? TrimAndFit(record) : result;
}

[[nodiscard]] HRESULT ExtractFromImageList(int iconIndex, ShortcutRecord& record) noexcept
{
    constexpr std::array lists{SHIL_JUMBO, SHIL_EXTRALARGE, SHIL_LARGE, SHIL_SMALL};
    for (int listKind : lists)
    {
        wil::com_ptr_nothrow<IImageList> list;
        HRESULT result = SHGetImageList(listKind, IID_PPV_ARGS(list.put()));
        if (FAILED(result))
        {
            continue;
        }
        HICON icon = nullptr;
        result = list->GetIcon(iconIndex, ILD_TRANSPARENT, &icon);
        wil::unique_hicon owned{icon};
        if (SUCCEEDED(result) && owned)
        {
            result = ExtractFromHicon(owned.get(), record);
            if (SUCCEEDED(result))
            {
                return result;
            }
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] HRESULT ExtractShellIcon(const wchar_t* target, ShortcutRecord& record) noexcept
{
    gExtractCalls.fetch_add(1, std::memory_order_relaxed);
    wil::com_ptr_nothrow<IShellItem> item;
    HRESULT result = SHCreateItemFromParsingName(target, nullptr, IID_PPV_ARGS(item.put()));
    if (SUCCEEDED(result))
    {
        wil::com_ptr_nothrow<IExtractIconW> extractIcon;
        if (SUCCEEDED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(extractIcon.put()))) && extractIcon)
        {
            std::array<wchar_t, MAX_PATH> iconPath{};
            int iconIndex = 0;
            UINT flags = 0;
            if (SUCCEEDED(extractIcon->GetIconLocation(GIL_FORSHELL, iconPath.data(),
                                                       static_cast<UINT>(iconPath.size()), &iconIndex, &flags)))
            {
                HICON largeIcon = nullptr;
                HICON smallIcon = nullptr;
                if (extractIcon->Extract(iconPath.data(), static_cast<UINT>(iconIndex), &largeIcon, &smallIcon,
                                         MAKELONG(kIconEdge, kIconEdge)) == S_OK)
                {
                    wil::unique_hicon ownedLarge{largeIcon};
                    wil::unique_hicon ownedSmall{smallIcon};
                    if (ownedLarge && SUCCEEDED(ExtractFromHicon(ownedLarge.get(), record)))
                    {
                        return S_OK;
                    }
                }
            }
        }
        wil::com_ptr_nothrow<IShellItemImageFactory> images;
        if (SUCCEEDED(item.query_to(images.put())) && images)
        {
            SIZE size{static_cast<LONG>(kIconEdge), static_cast<LONG>(kIconEdge)};
            HBITMAP bitmap = nullptr;
            if (SUCCEEDED(images->GetImage(size, SIIGBF_BIGGERSIZEOK, &bitmap)) && bitmap)
            {
                wil::unique_hbitmap owned{bitmap};
                if (SUCCEEDED(ExtractFromHbitmap(owned.get(), record)))
                {
                    return S_OK;
                }
            }
        }
    }

    SHFILEINFOW info{};
    if (SHGetFileInfoW(target, 0, &info, sizeof(info), SHGFI_SYSICONINDEX) != 0)
    {
        if (SUCCEEDED(ExtractFromImageList(info.iIcon, record)))
        {
            return S_OK;
        }
    }
    SHSTOCKICONINFO stock{};
    stock.cbSize = sizeof(stock);
    if (SUCCEEDED(SHGetStockIconInfo(SIID_APPLICATION, SHGSI_SYSICONINDEX, &stock)))
    {
        return ExtractFromImageList(stock.iSysImageIndex, record);
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

HRESULT ExtractShortcutIcon(ShortcutRecord& record) noexcept
{
    record.bgra.reset();
    record.sourceEdge = 0;
    if (record.iconPng[0] != L'\0')
    {
        if (SUCCEEDED(ExtractFromPng(record.iconPng, record)))
        {
            return S_OK;
        }
    }
    if (record.target[0] == L'\0')
    {
        return E_INVALIDARG;
    }
    return ExtractShellIcon(record.target, record);
}

void EnumerateLnks(const wchar_t* directory, LauncherConfiguration& configuration) noexcept
{
    if (!directory || directory[0] == L'\0' || configuration.count >= kMaximumShortcuts)
    {
        return;
    }
    std::array<wchar_t, 1024> pattern{};
    const int written = swprintf_s(pattern.data(), pattern.size(), L"%s\\*.lnk", directory);
    if (written <= 0)
    {
        return;
    }
    WIN32_FIND_DATAW data{};
    const HANDLE find = FindFirstFileW(pattern.data(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const auto closeFind = wil::scope_exit([&]() noexcept { FindClose(find); });
    std::array<std::array<wchar_t, MAX_PATH>, kMaximumShortcuts> names{};
    uint32_t found = 0;
    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || !EndsWithInsensitive(data.cFileName, L".lnk") ||
            CompareStringOrdinal(data.cFileName, -1, L"desktop.ini", -1, TRUE) == CSTR_EQUAL)
        {
            continue;
        }
        if (found < names.size())
        {
            CopyWide(data.cFileName, names[found].data(), names[found].size());
            ++found;
        }
    } while (FindNextFileW(find, &data) != FALSE && found < names.size());

    for (uint32_t i = 0; i < found; ++i)
    {
        for (uint32_t j = i + 1; j < found; ++j)
        {
            if (CompareStringOrdinal(names[j].data(), -1, names[i].data(), -1, TRUE) == CSTR_LESS_THAN)
            {
                std::swap(names[i], names[j]);
            }
        }
    }
    for (uint32_t index = 0; index < found && configuration.count < kMaximumShortcuts; ++index)
    {
        ShortcutRecord& item = configuration.items[configuration.count];
        item = ShortcutRecord{};
        const int pathWritten =
            swprintf_s(item.target, std::size(item.target), L"%s\\%s", directory, names[index].data());
        if (pathWritten <= 0 || !WideToUtf8(item.target, item.targetUtf8, std::size(item.targetUtf8)))
        {
            continue;
        }
        item.kind = 1;
        ++configuration.count;
    }
}

void ResolvePinDirectory(wchar_t* directory, size_t capacity) noexcept
{
    directory[0] = L'\0';
    AcquireSRWLockShared(&gPinLock);
    const PinMode mode = gPinMode;
    std::array<wchar_t, 1024> overridePath{};
    CopyWide(gPinOverride, overridePath.data(), overridePath.size());
    ReleaseSRWLockShared(&gPinLock);
    if (mode == PinMode::None || (mode == PinMode::Live && IsAutomatedHost()))
    {
        return;
    }
    if (mode == PinMode::Override)
    {
        CopyWide(overridePath.data(), directory, capacity);
        return;
    }
    PWSTR folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_UserPinned, 0, nullptr, &folder)) && folder)
    {
        wil::unique_cotaskmem_string owned{folder};
        if (swprintf_s(directory, capacity, L"%s\\TaskBar", owned.get()) > 0 &&
            GetFileAttributesW(directory) != INVALID_FILE_ATTRIBUTES)
        {
            return;
        }
    }
    directory[0] = L'\0';
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) || !roaming)
    {
        return;
    }
    wil::unique_cotaskmem_string owned{roaming};
    (void)swprintf_s(directory, capacity, L"%s\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar",
                     owned.get());
}

[[nodiscard]] bool ParseInstanceSettings(yyjson_val* instance, LauncherConfiguration& configuration) noexcept
{
    configuration = {};
    if (!instance || !yyjson_is_obj(instance))
    {
        return false;
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(instance);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name{yyjson_get_str(key), yyjson_get_len(key)};
        if (name != "shortcuts")
        {
            return false;
        }
    }
    yyjson_val* shortcuts = yyjson_obj_get(instance, "shortcuts");
    if (!shortcuts)
    {
        return true;
    }
    if (!yyjson_is_arr(shortcuts) || yyjson_arr_size(shortcuts) > kMaximumShortcuts)
    {
        return false;
    }
    const size_t count = yyjson_arr_size(shortcuts);
    for (size_t index = 0; index < count; ++index)
    {
        yyjson_val* item = yyjson_arr_get(shortcuts, index);
        if (!yyjson_is_obj(item))
        {
            return false;
        }
        yyjson_obj_iter itemIterator = yyjson_obj_iter_with(item);
        bool sawTarget = false;
        while (yyjson_val* key = yyjson_obj_iter_next(&itemIterator))
        {
            const std::string_view name{yyjson_get_str(key), yyjson_get_len(key)};
            if (name != "target" && name != "iconPng")
            {
                return false;
            }
            sawTarget = sawTarget || name == "target";
        }
        yyjson_val* targetValue = yyjson_obj_get(item, "target");
        yyjson_val* iconValue = yyjson_obj_get(item, "iconPng");
        if (!sawTarget || !yyjson_is_str(targetValue))
        {
            return false;
        }
        const std::string_view target{yyjson_get_str(targetValue), yyjson_get_len(targetValue)};
        const int kind = ClassifyTarget(target);
        if (kind == 0)
        {
            return false;
        }
        ShortcutRecord& record = configuration.items[configuration.count];
        record = ShortcutRecord{};
        if (!CopyNarrow(target, record.targetUtf8, std::size(record.targetUtf8)) ||
            !Utf8ToWide(target, record.target, std::size(record.target)))
        {
            return false;
        }
        record.kind = kind;
        if (iconValue)
        {
            if (!yyjson_is_str(iconValue) || yyjson_get_len(iconValue) > 260)
            {
                return false;
            }
            const std::string_view icon{yyjson_get_str(iconValue), yyjson_get_len(iconValue)};
            if (!icon.empty() && ClassifyTarget(icon) != 1)
            {
                return false;
            }
            if (!Utf8ToWide(icon, record.iconPng, std::size(record.iconPng)))
            {
                return false;
            }
        }
        for (uint32_t existing = 0; existing < configuration.count; ++existing)
        {
            if (kind == 2)
            {
                if (std::strcmp(configuration.items[existing].targetUtf8, record.targetUtf8) == 0)
                {
                    return false;
                }
            }
            else if (CompareStringOrdinal(configuration.items[existing].target, -1, record.target, -1, TRUE) ==
                     CSTR_EQUAL)
            {
                return false;
            }
        }
        ++configuration.count;
    }
    return true;
}

[[nodiscard]] HRESULT ReadFactoryConfiguration(const RedXeFactoryOptions* options,
                                               LauncherConfiguration& configuration) noexcept
{
    configuration = {};
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
    unique_yyjson_doc document{yyjson_read(options->configurationJsonUtf8, options->configurationBytes, 0)};
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!yyjson_is_obj(root))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    yyjson_val* plugin = yyjson_obj_get(root, "plugin");
    yyjson_val* instance = yyjson_obj_get(root, "instance");
    if (!yyjson_is_obj(plugin) || yyjson_obj_size(plugin) != 0 || !yyjson_is_obj(instance))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return ParseInstanceSettings(instance, configuration) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

class LauncherInstanceGpu final
{
  public:
    [[nodiscard]] HRESULT Initialize(ID3D11Device* device) noexcept
    {
        if (!device)
        {
            return E_POINTER;
        }
        if (_deviceIdentity == device && _constantBuffer && _texture)
        {
            return S_OK;
        }
        Reset();
        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth = sizeof(LauncherConstants);
        constantDescription.Usage = D3D11_USAGE_DYNAMIC;
        constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        wil::com_ptr_nothrow<ID3D11Buffer> constantBuffer;
        HRESULT result = device->CreateBuffer(&constantDescription, nullptr, constantBuffer.put());
        if (FAILED(result))
        {
            return result;
        }
        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = kIconEdge;
        textureDescription.Height = kIconEdge;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = kMaximumShortcuts;
        textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        wil::com_ptr_nothrow<ID3D11Texture2D> texture;
        result = device->CreateTexture2D(&textureDescription, nullptr, texture.put());
        if (FAILED(result))
        {
            return result;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = textureDescription.Format;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        viewDescription.Texture2DArray.MipLevels = 1;
        viewDescription.Texture2DArray.ArraySize = kMaximumShortcuts;
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> view;
        result = device->CreateShaderResourceView(texture.get(), &viewDescription, view.put());
        if (FAILED(result))
        {
            return result;
        }
        _deviceIdentity = device;
        _constantBuffer = std::move(constantBuffer);
        _texture = std::move(texture);
        _view = std::move(view);
        return S_OK;
    }

    void Reset() noexcept
    {
        _view.reset();
        _texture.reset();
        _constantBuffer.reset();
        _deviceIdentity = nullptr;
    }

    [[nodiscard]] HRESULT UploadIcons(ID3D11DeviceContext* context, const LauncherConfiguration& display) noexcept
    {
        if (!context || !_texture)
        {
            return E_UNEXPECTED;
        }
        gTextureUploads.fetch_add(1, std::memory_order_relaxed);
        const UINT pitch = kIconEdge * 4;
        for (uint32_t index = 0; index < kMaximumShortcuts; ++index)
        {
            std::array<uint8_t, kIconEdge * kIconEdge * 4> empty{};
            const uint8_t* source = empty.data();
            if (index < display.count && display.items[index].bgra)
            {
                source = display.items[index].bgra.get();
            }
            context->UpdateSubresource(_texture.get(), D3D11CalcSubresource(0, index, 1), nullptr, source, pitch,
                                       pitch * kIconEdge);
        }
        return S_OK;
    }

    [[nodiscard]] ID3D11Buffer* ConstantBuffer() const noexcept
    {
        return _constantBuffer.get();
    }

    [[nodiscard]] ID3D11ShaderResourceView* View() const noexcept
    {
        return _view.get();
    }

  private:
    ID3D11Device* _deviceIdentity = nullptr;
    wil::com_ptr_nothrow<ID3D11Buffer> _constantBuffer;
    wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _view;
};

class LauncherDeviceResources final
{
  public:
    [[nodiscard]] HRESULT Initialize(ID3D11Device* device) noexcept
    {
        if (!device)
        {
            return E_POINTER;
        }
        if (_deviceIdentity == device && _vertexShader)
        {
            return S_OK;
        }
        Reset();
        wil::com_ptr_nothrow<ID3D11VertexShader> vertexShader;
        HRESULT result = device->CreateVertexShader(g_LauncherVertexShader, sizeof(g_LauncherVertexShader), nullptr,
                                                    vertexShader.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<ID3D11PixelShader> pixelShader;
        result =
            device->CreatePixelShader(g_LauncherPixelShader, sizeof(g_LauncherPixelShader), nullptr, pixelShader.put());
        if (FAILED(result))
        {
            return result;
        }
        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        wil::com_ptr_nothrow<ID3D11SamplerState> sampler;
        result = device->CreateSamplerState(&samplerDescription, sampler.put());
        if (FAILED(result))
        {
            return result;
        }
        D3D11_BLEND_DESC blendDescription{};
        blendDescription.RenderTarget[0].BlendEnable = TRUE;
        blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        wil::com_ptr_nothrow<ID3D11BlendState> blend;
        result = device->CreateBlendState(&blendDescription, blend.put());
        if (FAILED(result))
        {
            return result;
        }
        D3D11_RASTERIZER_DESC rasterDescription{};
        rasterDescription.FillMode = D3D11_FILL_SOLID;
        rasterDescription.CullMode = D3D11_CULL_NONE;
        rasterDescription.DepthClipEnable = TRUE;
        wil::com_ptr_nothrow<ID3D11RasterizerState> rasterizer;
        result = device->CreateRasterizerState(&rasterDescription, rasterizer.put());
        if (FAILED(result))
        {
            return result;
        }
        D3D11_DEPTH_STENCIL_DESC depthDescription{};
        wil::com_ptr_nothrow<ID3D11DepthStencilState> depth;
        result = device->CreateDepthStencilState(&depthDescription, depth.put());
        if (FAILED(result))
        {
            return result;
        }
        _deviceIdentity = device;
        _vertexShader = std::move(vertexShader);
        _pixelShader = std::move(pixelShader);
        _sampler = std::move(sampler);
        _blend = std::move(blend);
        _rasterizer = std::move(rasterizer);
        _depth = std::move(depth);
        return S_OK;
    }

    void Reset() noexcept
    {
        _depth.reset();
        _rasterizer.reset();
        _blend.reset();
        _sampler.reset();
        _pixelShader.reset();
        _vertexShader.reset();
        _deviceIdentity = nullptr;
    }

    [[nodiscard]] HRESULT Draw(ID3D11DeviceContext* context, LauncherInstanceGpu& instance,
                               LauncherConstants constants) noexcept
    {
        if (!context || !_vertexShader || !instance.ConstantBuffer() || !instance.View())
        {
            return E_UNEXPECTED;
        }
        ID3D11Buffer* constantBuffer = instance.ConstantBuffer();
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT result = context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
        {
            return result;
        }
        constants.drawMode = 0.0f;
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(constantBuffer, 0);

        ID3D11Buffer* constantsBuffers[] = {constantBuffer};
        ID3D11SamplerState* samplers[] = {_sampler.get()};
        ID3D11ShaderResourceView* views[] = {instance.View()};
        constexpr float blendFactor[4]{};
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        context->VSSetShader(_vertexShader.get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, constantsBuffers);
        context->PSSetShader(_pixelShader.get(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, constantsBuffers);
        context->PSSetSamplers(0, 1, samplers);
        context->PSSetShaderResources(0, 1, views);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->RSSetState(_rasterizer.get());
        context->OMSetBlendState(_blend.get(), blendFactor, UINT_MAX);
        context->OMSetDepthStencilState(_depth.get(), 0);
        context->Draw(3, 0);
        uint32_t draws = 1;
        if (constants.iconCount > 0)
        {
            result = context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(result))
            {
                return result;
            }
            constants.drawMode = 1.0f;
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            context->Unmap(constantBuffer, 0);
            context->DrawInstanced(6, constants.iconCount, 0, 0);
            ++draws;
        }
        gDrawCalls.fetch_add(draws, std::memory_order_relaxed);
        gLastDrawCount.store(draws, std::memory_order_relaxed);
        gLastInstanceCount.store(constants.iconCount, std::memory_order_relaxed);
        return S_OK;
    }

  private:
    ID3D11Device* _deviceIdentity = nullptr;
    wil::com_ptr_nothrow<ID3D11VertexShader> _vertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _pixelShader;
    wil::com_ptr_nothrow<ID3D11SamplerState> _sampler;
    wil::com_ptr_nothrow<ID3D11BlendState> _blend;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _rasterizer;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _depth;
};

void CopyShortcutIdentity(const ShortcutRecord& source, ShortcutRecord& destination) noexcept
{
    destination.kind = source.kind;
    CopyWide(source.target, destination.target, std::size(destination.target));
    CopyWide(source.iconPng, destination.iconPng, std::size(destination.iconPng));
    CopyNarrow(source.targetUtf8, destination.targetUtf8, std::size(destination.targetUtf8));
}

void CopyConfigurationIdentity(const LauncherConfiguration& source, LauncherConfiguration& destination) noexcept
{
    destination = {};
    destination.count = source.count;
    for (uint32_t index = 0; index < source.count; ++index)
    {
        CopyShortcutIdentity(source.items[index], destination.items[index]);
    }
}

class LauncherWidget final
    : public RedXeComObject<LauncherWidget, IRedXeWidget, IRedXeGpuWidget, IRedXeInteractiveWidget, IRedXeRaisedWidget>
{
  public:
    LauncherWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner, LauncherDeviceResources& resources,
                   IRedXeHost* host, const char* instanceId, const LauncherConfiguration& configuration) noexcept
        : _providerOwner(std::move(providerOwner)), _resources(&resources), _host(host)
    {
        CopyConfigurationIdentity(configuration, _authored);
        CopyNarrow(instanceId ? std::string_view(instanceId) : std::string_view{}, _instanceId, std::size(_instanceId));
        LARGE_INTEGER frequency{};
        if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
        {
            _qpcFrequency = static_cast<uint64_t>(frequency.QuadPart);
        }
        gLiveWidgets.fetch_add(1, std::memory_order_relaxed);
        PublishCounts();
    }

    ~LauncherWidget()
    {
        gLiveWidgets.fetch_sub(1, std::memory_order_relaxed);
    }

    HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept override
    {
        _visible = visible != FALSE;
        if (!_visible)
        {
            _launchActive = false;
            _pointerDown = false;
            return S_OK;
        }
        RefreshDisplay(false);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CollectPersistentSettings(char* jsonUtf8, uint32_t capacityBytes,
                                                        uint32_t* writtenBytes) noexcept override
    {
        if (writtenBytes)
        {
            *writtenBytes = 0;
        }
        if (!writtenBytes)
        {
            return E_POINTER;
        }
        if (!_dirty)
        {
            return S_FALSE;
        }
        uint32_t written = 0;
        const HRESULT result = WriteAuthoredJson(jsonUtf8, capacityBytes, written);
        if (SUCCEEDED(result))
        {
            *writtenBytes = written;
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE GetRaisedExtent(RedXeRaisedExtent* extent) noexcept override
    {
        if (!extent)
        {
            return E_POINTER;
        }
        *extent = RedXeRaisedExtentHalf;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetRaised(BOOL) noexcept override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept override
    {
        if (!context)
        {
            return E_POINTER;
        }
        if (context->sizeBytes != sizeof(RedXeGpuDeviceContext) || !context->device)
        {
            return E_INVALIDARG;
        }
        HRESULT result = _resources->Initialize(context->device);
        if (FAILED(result))
        {
            return result;
        }
        result = _gpu.Initialize(context->device);
        if (FAILED(result))
        {
            return result;
        }
        _device = context->device;
        wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
        context->device->GetImmediateContext(immediate.put());
        result = _gpu.UploadIcons(immediate.get(), _display);
        _deviceReady = SUCCEEDED(result);
        return result;
    }

    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        _deviceReady = false;
        _device = nullptr;
        _gpu.Reset();
        _resources->Reset();
    }

    HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept override
    {
        if (!context)
        {
            return E_POINTER;
        }
        if (context->sizeBytes != sizeof(RedXeGpuTargetSizeContext) || context->widthPixels == 0 ||
            context->heightPixels == 0)
        {
            return E_INVALIDARG;
        }
        _width = context->widthPixels;
        _height = context->heightPixels;
        _dpi = context->dpi == 0 ? USER_DEFAULT_SCREEN_DPI : context->dpi;
        ComputeGrid();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept override
    {
        if (!context || !context->widget)
        {
            return E_POINTER;
        }
        const RedXeWidgetFrameContext& widget = *context->widget;
        if (context->sizeBytes != sizeof(RedXeGpuFrameContext) || widget.sizeBytes != sizeof(RedXeWidgetFrameContext) ||
            !context->deviceContext || widget.widthPixels == 0 || widget.heightPixels == 0 ||
            !std::isfinite(widget.elapsedSeconds) || !std::isfinite(widget.deltaSeconds) || widget.deltaSeconds < 0.0f)
        {
            return E_INVALIDARG;
        }
        if (!_deviceReady)
        {
            return E_UNEXPECTED;
        }
        float launchAmount = 0.0f;
        if (_launchActive)
        {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const double elapsed = _qpcFrequency == 0
                                       ? 1.0
                                       : static_cast<double>(static_cast<uint64_t>(now.QuadPart) - _launchStartQpc) /
                                             static_cast<double>(_qpcFrequency);
            if (elapsed >= kLaunchDurationSeconds)
            {
                _launchActive = false;
            }
            else
            {
                launchAmount = static_cast<float>(elapsed / kLaunchDurationSeconds);
                if (_host)
                {
                    (void)_host->RequestFrame();
                }
            }
        }
        LauncherConstants constants{};
        constants.viewportWidth = static_cast<float>(widget.widthPixels);
        constants.viewportHeight = static_cast<float>(widget.heightPixels);
        constants.hint = (_display.count == 0) ? 1.0f : 0.0f;
        constants.background[0] = 0.07f;
        constants.background[1] = 0.07f;
        constants.background[2] = 0.08f;
        constants.background[3] = 1.0f;
        constants.hintColor[0] = 0.55f;
        constants.hintColor[1] = 0.58f;
        constants.hintColor[2] = 0.62f;
        constants.hintColor[3] = 1.0f;
        constants.iconCount = _display.count;
        for (uint32_t index = 0; index < _display.count; ++index)
        {
            constants.iconRect[index][0] = _cells[index][0];
            constants.iconRect[index][1] = _cells[index][1];
            constants.iconRect[index][2] = _cells[index][2];
            constants.iconRect[index][3] = _cells[index][3];
            float dim = 1.0f;
            float tilt = 0.0f;
            float z = 0.0f;
            if (_launchActive && index == _launchIndex)
            {
                tilt = launchAmount * 0.65f;
                z = launchAmount * 48.0f;
            }
            else if (_launchActive)
            {
                dim = 1.0f - launchAmount * 0.45f;
            }
            if (_dragHighlight && index == _hoverIndex)
            {
                dim *= 1.08f;
            }
            constants.iconMotion[index][0] = tilt;
            constants.iconMotion[index][1] = z;
            constants.iconMotion[index][2] = dim;
            constants.iconMotion[index][3] = static_cast<float>(index);
        }
        return _resources->Draw(context->deviceContext, _gpu, constants);
    }

    HRESULT STDMETHODCALLTYPE OnPointer(const RedXePointerEvent* event) noexcept override
    {
        if (!event)
        {
            return E_POINTER;
        }
        if (event->sizeBytes != sizeof(RedXePointerEvent))
        {
            return E_INVALIDARG;
        }
        if (event->phase == RedXePointerPhaseCancel)
        {
            _pointerDown = false;
            _downIndex = kMaximumShortcuts;
            return S_FALSE;
        }
        const uint32_t hit = HitCell(event->x, event->y);
        if (event->phase == RedXePointerPhaseDown)
        {
            _pointerDown = hit < _display.count;
            _downIndex = hit;
            return _pointerDown ? S_OK : S_FALSE;
        }
        if (event->phase == RedXePointerPhaseUp)
        {
            const bool consume = _pointerDown && hit == _downIndex && hit < _display.count;
            _pointerDown = false;
            if (consume)
            {
                Launch(hit);
                return S_OK;
            }
            return S_FALSE;
        }
        return hit < _display.count ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnDragOver(float x, float y) noexcept override
    {
        _dragHighlight = true;
        _hoverIndex = HitCell(x, y);
        if (_host)
        {
            (void)_host->RequestFrame();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDragLeave() noexcept override
    {
        _dragHighlight = false;
        if (_host)
        {
            (void)_host->RequestFrame();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDrop(const RedXeDropEvent* event) noexcept override
    {
        _dragHighlight = false;
        if (!event)
        {
            return E_POINTER;
        }
        if (event->sizeBytes != sizeof(RedXeDropEvent) || event->itemCount == 0 ||
            event->itemCount > kRedXeMaximumDropItems || !event->items)
        {
            return E_INVALIDARG;
        }
        LauncherConfiguration previous{};
        CopyConfigurationIdentity(_authored, previous);
        uint32_t accepted = 0;
        for (uint32_t index = 0; index < event->itemCount && _authored.count < kMaximumShortcuts; ++index)
        {
            const RedXeDropItem& item = event->items[index];
            if (item.sizeBytes != sizeof(RedXeDropItem) || !item.target || item.target[0] == L'\0')
            {
                continue;
            }
            ShortcutRecord record{};
            if (!CopyWide(item.target, record.target, std::size(record.target)) ||
                !WideToUtf8(record.target, record.targetUtf8, std::size(record.targetUtf8)))
            {
                continue;
            }
            record.kind = ClassifyTarget(record.targetUtf8);
            if (record.kind == 0)
            {
                continue;
            }
            bool duplicate = false;
            for (uint32_t existing = 0; existing < _authored.count; ++existing)
            {
                if (record.kind == 2)
                {
                    duplicate = std::strcmp(_authored.items[existing].targetUtf8, record.targetUtf8) == 0;
                }
                else
                {
                    duplicate = CompareStringOrdinal(_authored.items[existing].target, -1, record.target, -1, TRUE) ==
                                CSTR_EQUAL;
                }
                if (duplicate)
                {
                    break;
                }
            }
            if (duplicate)
            {
                continue;
            }
            _authored.items[_authored.count] = std::move(record);
            ++_authored.count;
            ++accepted;
        }
        if (accepted == 0)
        {
            return S_FALSE;
        }
        _dirty = true;
        RefreshDisplay(false);
        if (_host)
        {
            std::array<char, kSettingsJsonCapacity + 1> json{};
            uint32_t written = 0;
            const HRESULT writtenResult = WriteAuthoredJson(json.data(), kSettingsJsonCapacity, written);
            if (FAILED(writtenResult) || FAILED(_host->PersistWidgetSettings(_instanceId, json.data(), written)))
            {
                CopyConfigurationIdentity(previous, _authored);
                _dirty = false;
                RefreshDisplay(false);
                return E_FAIL;
            }
            _dirty = false;
            (void)_host->RequestFrame();
        }
        return S_OK;
    }

  private:
    void PublishCounts() noexcept
    {
        gAuthoredCount.store(_authored.count, std::memory_order_relaxed);
        gDisplayCount.store(_display.count, std::memory_order_relaxed);
        gUsingPins.store(_usingPins ? 1U : 0U, std::memory_order_relaxed);
        gColumns.store(_columns, std::memory_order_relaxed);
        gRows.store(_rows, std::memory_order_relaxed);
    }

    void ComputeGrid() noexcept
    {
        const uint32_t n = _display.count;
        if (n == 0 || _width == 0 || _height == 0)
        {
            _columns = 0;
            _rows = 0;
            PublishCounts();
            return;
        }
        const float aspect = static_cast<float>(_width) / static_cast<float>(_height);
        const long rounded = std::lround(std::sqrt(static_cast<float>(n) * aspect));
        _columns = static_cast<uint32_t>(std::clamp(rounded, 1L, static_cast<long>(n)));
        _rows = (n + _columns - 1U) / _columns;
        const float cellWidth = static_cast<float>(_width) / static_cast<float>(_columns);
        const float cellHeight = static_cast<float>(_height) / static_cast<float>(_rows);
        const float padding = std::max(6.0f, static_cast<float>(_dpi) * 10.0f / 96.0f);
        const float icon = std::max(8.0f, std::min(cellWidth, cellHeight) - padding * 2.0f);
        const float usedWidth = static_cast<float>(_columns) * cellWidth;
        const float usedHeight = static_cast<float>(_rows) * cellHeight;
        const float originX = (static_cast<float>(_width) - usedWidth) * 0.5f;
        const float originY = (static_cast<float>(_height) - usedHeight) * 0.5f;
        for (uint32_t index = 0; index < n; ++index)
        {
            const uint32_t column = index % _columns;
            const uint32_t row = index / _columns;
            _cells[index][0] = originX + (static_cast<float>(column) + 0.5f) * cellWidth;
            _cells[index][1] = originY + (static_cast<float>(row) + 0.5f) * cellHeight;
            _cells[index][2] = icon * 0.5f;
            _cells[index][3] = icon * 0.5f;
        }
        PublishCounts();
    }

    [[nodiscard]] uint32_t HitCell(float x, float y) const noexcept
    {
        for (uint32_t index = 0; index < _display.count; ++index)
        {
            if (std::fabs(x - _cells[index][0]) <= _cells[index][2] &&
                std::fabs(y - _cells[index][1]) <= _cells[index][3])
            {
                return index;
            }
        }
        return kMaximumShortcuts;
    }

    void RefreshDisplay(bool requestFrame) noexcept
    {
        if (_authored.count > 0)
        {
            _display = {};
            _display.count = _authored.count;
            for (uint32_t index = 0; index < _authored.count; ++index)
            {
                CopyShortcutIdentity(_authored.items[index], _display.items[index]);
            }
            _usingPins = false;
        }
        else
        {
            _display = {};
            std::array<wchar_t, 1024> directory{};
            ResolvePinDirectory(directory.data(), directory.size());
            EnumerateLnks(directory.data(), _display);
            _usingPins = _display.count > 0;
        }
        bool degraded = false;
        for (uint32_t index = 0; index < _display.count; ++index)
        {
            if (FAILED(ExtractShortcutIcon(_display.items[index])))
            {
                degraded = true;
            }
        }
        (void)degraded;
        ComputeGrid();
        if (_deviceReady && _device)
        {
            wil::com_ptr_nothrow<ID3D11DeviceContext> immediate;
            _device->GetImmediateContext(immediate.put());
            (void)_gpu.UploadIcons(immediate.get(), _display);
        }
        PublishCounts();
        if (requestFrame && _host)
        {
            (void)_host->RequestFrame();
        }
    }

    void Launch(uint32_t index) noexcept
    {
        if (_launchActive || index >= _display.count)
        {
            return;
        }
        const ShortcutRecord& item = _display.items[index];
        gLaunchCount.fetch_add(1, std::memory_order_relaxed);
        gLastLaunchKind.store(static_cast<uint32_t>(item.kind), std::memory_order_relaxed);
        gLastShellMask.store(SEE_MASK_FLAG_NO_UI, std::memory_order_relaxed);
        gLastVerbWasNull.store(1, std::memory_order_relaxed);
        gLastShow.store(SW_SHOWNORMAL, std::memory_order_relaxed);
        if (!IsAutomatedHost())
        {
            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_FLAG_NO_UI;
            info.lpFile = item.target;
            info.nShow = SW_SHOWNORMAL;
            std::array<wchar_t, kTargetCapacity> directory{};
            if (item.kind == 1)
            {
                const DWORD attributes = GetFileAttributesW(item.target);
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    CopyWide(item.target, directory.data(), directory.size());
                    wchar_t* slash = std::wcsrchr(directory.data(), L'\\');
                    if (!slash)
                    {
                        slash = std::wcsrchr(directory.data(), L'/');
                    }
                    if (slash)
                    {
                        *slash = L'\0';
                        info.lpDirectory = directory.data();
                    }
                }
            }
            if (ShellExecuteExW(&info))
            {
                gShellExecuteCount.fetch_add(1, std::memory_order_relaxed);
            }
            else if (_host)
            {
                const RedXeWidgetStatusReport report{sizeof(RedXeWidgetStatusReport), RedXeWidgetStatusDegraded,
                                                     L"Launch failed"};
                (void)_host->ReportWidgetStatus(_instanceId, &report);
            }
        }
        _launchActive = true;
        _launchIndex = index;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        _launchStartQpc = static_cast<uint64_t>(now.QuadPart);
        if (_host)
        {
            (void)_host->RequestFrame();
        }
    }

    [[nodiscard]] HRESULT WriteAuthoredJson(char* jsonUtf8, uint32_t capacityBytes, uint32_t& written) noexcept
    {
        written = 0;
        if (!jsonUtf8 || capacityBytes == 0)
        {
            return E_INVALIDARG;
        }
        unique_yyjson_mut_doc document{yyjson_mut_doc_new(nullptr)};
        yyjson_mut_val* root = document ? yyjson_mut_obj(document.get()) : nullptr;
        yyjson_mut_val* array = document ? yyjson_mut_arr(document.get()) : nullptr;
        if (!root || !array)
        {
            return E_OUTOFMEMORY;
        }
        yyjson_mut_doc_set_root(document.get(), root);
        for (uint32_t index = 0; index < _authored.count; ++index)
        {
            yyjson_mut_val* item = yyjson_mut_obj(document.get());
            if (!item || !yyjson_mut_obj_add_strcpy(document.get(), item, "target", _authored.items[index].targetUtf8))
            {
                return E_OUTOFMEMORY;
            }
            if (_authored.items[index].iconPng[0] != L'\0')
            {
                std::array<char, kIconPngCapacity> iconUtf8{};
                if (!WideToUtf8(_authored.items[index].iconPng, iconUtf8.data(), iconUtf8.size()) ||
                    !yyjson_mut_obj_add_strcpy(document.get(), item, "iconPng", iconUtf8.data()))
                {
                    return E_OUTOFMEMORY;
                }
            }
            if (!yyjson_mut_arr_add_val(array, item))
            {
                return E_OUTOFMEMORY;
            }
        }
        if (!yyjson_mut_obj_add_val(document.get(), root, "shortcuts", array))
        {
            return E_OUTOFMEMORY;
        }
        size_t length = 0;
        unique_malloc_string text{yyjson_mut_write(document.get(), 0, &length)};
        if (!text || length == 0 || length >= capacityBytes)
        {
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        std::memcpy(jsonUtf8, text.get(), length);
        jsonUtf8[length] = '\0';
        written = static_cast<uint32_t>(length);
        return S_OK;
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    LauncherDeviceResources* _resources = nullptr;
    LauncherInstanceGpu _gpu;
    ID3D11Device* _device = nullptr;
    IRedXeHost* _host = nullptr;
    char _instanceId[kInstanceIdCapacity]{};
    LauncherConfiguration _authored{};
    LauncherConfiguration _display{};
    std::array<std::array<float, 4>, kMaximumShortcuts> _cells{};
    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _dpi = USER_DEFAULT_SCREEN_DPI;
    uint32_t _columns = 0;
    uint32_t _rows = 0;
    uint32_t _downIndex = kMaximumShortcuts;
    uint32_t _launchIndex = kMaximumShortcuts;
    uint32_t _hoverIndex = kMaximumShortcuts;
    uint64_t _launchStartQpc = 0;
    uint64_t _qpcFrequency = 0;
    bool _visible = false;
    bool _deviceReady = false;
    bool _usingPins = false;
    bool _pointerDown = false;
    bool _launchActive = false;
    bool _dragHighlight = false;
    bool _dirty = false;
};

class LauncherProvider final : public RedXeComObject<LauncherProvider, IRedXeWidgetProvider>
{
  public:
    LauncherProvider(const LauncherConfiguration& configuration, IRedXeHost* host) noexcept : _host(host)
    {
        CopyConfigurationIdentity(configuration, _configuration);
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
        if (widget)
        {
            *widget = nullptr;
        }
        if (!widget)
        {
            return E_POINTER;
        }
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
        auto* created =
            new (std::nothrow) LauncherWidget(std::move(providerOwner), _resources, _host, instanceId, _configuration);
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    LauncherConfiguration _configuration;
    LauncherDeviceResources _resources;
    IRedXeHost* _host = nullptr;
};

HRESULT CreateLauncherProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                               void** result) noexcept
{
    if (!result)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
    }
    LauncherConfiguration configuration{};
    const HRESULT parsed = ReadFactoryConfiguration(options, configuration);
    if (FAILED(parsed))
    {
        return parsed;
    }
    auto* provider = new (std::nothrow) LauncherProvider(configuration, host);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = static_cast<IRedXeWidgetProvider*>(provider);
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateLauncherProvider},
};
} // namespace

extern "C" HRESULT __stdcall RedXeCreate(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                         const char* pluginId, void** result) noexcept
{
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<uint32_t>(kMetadata.size()), metadata, count);
}

extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* pluginId,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetStaticPluginSettingsContract(kPluginId, pluginId, &kSettingsContract, contract);
}

extern "C" void __stdcall RedXePluginShutdown() noexcept {}

extern "C" HRESULT __stdcall RedXeLauncherSetTestPinDirectory(const wchar_t* directory) noexcept
{
    AcquireSRWLockExclusive(&gPinLock);
    if (!directory)
    {
        gPinMode = PinMode::Live;
        gPinOverride[0] = L'\0';
    }
    else if (directory[0] == L'\0')
    {
        gPinMode = PinMode::None;
        gPinOverride[0] = L'\0';
    }
    else
    {
        if (!CopyWide(directory, gPinOverride, std::size(gPinOverride)))
        {
            ReleaseSRWLockExclusive(&gPinLock);
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        gPinMode = PinMode::Override;
    }
    ReleaseSRWLockExclusive(&gPinLock);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeLauncherGetTestDiagnostics(LauncherTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(LauncherTestDiagnostics))
    {
        return E_INVALIDARG;
    }
    diagnostics->authoredCount = gAuthoredCount.load(std::memory_order_relaxed);
    diagnostics->displayCount = gDisplayCount.load(std::memory_order_relaxed);
    diagnostics->usingTaskbarPins = gUsingPins.load(std::memory_order_relaxed);
    diagnostics->columns = gColumns.load(std::memory_order_relaxed);
    diagnostics->rows = gRows.load(std::memory_order_relaxed);
    diagnostics->lastInstanceCount = gLastInstanceCount.load(std::memory_order_relaxed);
    diagnostics->lastDrawCount = gLastDrawCount.load(std::memory_order_relaxed);
    diagnostics->lastLaunchKind = gLastLaunchKind.load(std::memory_order_relaxed);
    diagnostics->lastShellMask = gLastShellMask.load(std::memory_order_relaxed);
    diagnostics->lastVerbWasNull = gLastVerbWasNull.load(std::memory_order_relaxed);
    diagnostics->lastShow = gLastShow.load(std::memory_order_relaxed);
    diagnostics->extractCalls = gExtractCalls.load(std::memory_order_relaxed);
    diagnostics->textureUploads = gTextureUploads.load(std::memory_order_relaxed);
    diagnostics->drawCalls = gDrawCalls.load(std::memory_order_relaxed);
    diagnostics->launchCount = gLaunchCount.load(std::memory_order_relaxed);
    diagnostics->shellExecuteCount = gShellExecuteCount.load(std::memory_order_relaxed);
    diagnostics->largestIconEdge = gLargestIconEdge.load(std::memory_order_relaxed);
    diagnostics->liveWidgets = gLiveWidgets.load(std::memory_order_relaxed);
    return S_OK;
}

extern "C" void __stdcall RedXeLauncherResetTestDiagnostics() noexcept
{
    gExtractCalls.store(0, std::memory_order_relaxed);
    gTextureUploads.store(0, std::memory_order_relaxed);
    gDrawCalls.store(0, std::memory_order_relaxed);
    gLaunchCount.store(0, std::memory_order_relaxed);
    gShellExecuteCount.store(0, std::memory_order_relaxed);
    gLastLaunchKind.store(0, std::memory_order_relaxed);
    gLargestIconEdge.store(0, std::memory_order_relaxed);
}
