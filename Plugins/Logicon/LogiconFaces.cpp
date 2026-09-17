#include "LogiconFaces.h"

#include "Actions/FluentGlyphNames.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Logicon
{
namespace
{
constexpr wchar_t kTextFamily[] = L"Segoe UI";
constexpr wchar_t kIconFamily[] = L"Segoe Fluent Icons";
constexpr wchar_t kLegacyIconFamily[] = L"Segoe MDL2 Assets";
constexpr float kLabelEm = 19.0f;
constexpr float kBigEm = 36.0f;
constexpr float kIconEm = 54.0f;
constexpr float kFaceSizeF = static_cast<float>(kFaceSize);
constexpr float kLabelMaxWidth = kFaceSizeF - 10.0f;
constexpr wchar_t kEllipsis = L'\x2026';
constexpr uint32_t kMaximumRunGlyphs = kMaximumFaceLabelCharacters + 1;

[[nodiscard]] uint8_t Channel(uint32_t rgb, uint32_t shift) noexcept
{
    return static_cast<uint8_t>((rgb >> shift) & 0xFFU);
}

[[nodiscard]] uint32_t PackBgra(uint32_t rgb) noexcept
{
    // Memory order B, G, R, A on little-endian: the uint32 is 0xAARRGGBB.
    return 0xFF000000U | (rgb & 0x00FFFFFFU);
}

void BlendPixel(uint32_t& destination, uint32_t rgb, uint32_t coverage) noexcept
{
    if (coverage == 0)
    {
        return;
    }
    if (coverage >= 255)
    {
        destination = PackBgra(rgb);
        return;
    }
    const uint32_t inverse = 255U - coverage;
    const uint32_t dr = (destination >> 16U) & 0xFFU;
    const uint32_t dg = (destination >> 8U) & 0xFFU;
    const uint32_t db = destination & 0xFFU;
    const uint32_t r = (Channel(rgb, 16) * coverage + dr * inverse + 127U) / 255U;
    const uint32_t g = (Channel(rgb, 8) * coverage + dg * inverse + 127U) / 255U;
    const uint32_t b = (Channel(rgb, 0) * coverage + db * inverse + 127U) / 255U;
    destination = 0xFF000000U | (r << 16U) | (g << 8U) | b;
}

void BlendPremultiplied(uint32_t& destination, uint32_t source) noexcept
{
    const uint32_t alpha = source >> 24U;
    if (alpha == 0)
    {
        return;
    }
    if (alpha >= 255)
    {
        destination = 0xFF000000U | (source & 0x00FFFFFFU);
        return;
    }
    const uint32_t inverse = 255U - alpha;
    const uint32_t r = ((source >> 16U) & 0xFFU) + (((destination >> 16U) & 0xFFU) * inverse + 127U) / 255U;
    const uint32_t g = ((source >> 8U) & 0xFFU) + (((destination >> 8U) & 0xFFU) * inverse + 127U) / 255U;
    const uint32_t b = (source & 0xFFU) + ((destination & 0xFFU) * inverse + 127U) / 255U;
    destination = 0xFF000000U | (std::min(r, 255U) << 16U) | (std::min(g, 255U) << 8U) | std::min(b, 255U);
}

void FillRect(uint32_t* bgra, int left, int top, int right, int bottom, uint32_t rgb) noexcept
{
    left = std::max(left, 0);
    top = std::max(top, 0);
    right = std::min(right, static_cast<int>(kFaceSize));
    bottom = std::min(bottom, static_cast<int>(kFaceSize));
    const uint32_t packed = PackBgra(rgb);
    for (int y = top; y < bottom; ++y)
    {
        for (int x = left; x < right; ++x)
        {
            bgra[static_cast<size_t>(y) * kFaceSize + static_cast<size_t>(x)] = packed;
        }
    }
}
} // namespace

wchar_t FluentGlyphFromName(const char* name, uint32_t bytes) noexcept
{
    return name && bytes != 0 ? RedXeActions::FluentGlyphFromName(std::string_view(name, bytes)) : 0;
}

uint32_t FluentGlyphNameCount() noexcept
{
    return RedXeActions::FluentGlyphNameCount();
}

const char* FluentGlyphNameAt(uint32_t index) noexcept
{
    return RedXeActions::FluentGlyphNameAt(index);
}

FaceRenderer::~FaceRenderer()
{
    Reset();
}

HRESULT FaceRenderer::CreateFace(const wchar_t* family, DWRITE_FONT_WEIGHT weight,
                                 wil::com_ptr_nothrow<IDWriteFontFace>& face) noexcept
{
    face.reset();
    wil::com_ptr_nothrow<IDWriteFontCollection> collection;
    HRESULT result = _factory->GetSystemFontCollection(collection.put(), FALSE);
    if (FAILED(result))
    {
        return result;
    }
    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    result = collection->FindFamilyName(family, &familyIndex, &exists);
    if (FAILED(result) || !exists)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    wil::com_ptr_nothrow<IDWriteFontFamily> fontFamily;
    result = collection->GetFontFamily(familyIndex, fontFamily.put());
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IDWriteFont> font;
    result = fontFamily->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.put());
    if (FAILED(result))
    {
        return result;
    }
    return font->CreateFontFace(face.put());
}

HRESULT FaceRenderer::Initialize() noexcept
{
    if (Ready())
    {
        return S_OK;
    }
    Reset();
    _dwriteModule.reset(LoadLibraryExW(L"dwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!_dwriteModule)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    using DWriteCreateFactoryFn = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    const auto createFactory =
        reinterpret_cast<DWriteCreateFactoryFn>(GetProcAddress(_dwriteModule.get(), "DWriteCreateFactory"));
    if (!createFactory)
    {
        Reset();
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    HRESULT result = createFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(_factory.put()));
    if (FAILED(result))
    {
        Reset();
        return result;
    }
    result = CreateFace(kTextFamily, DWRITE_FONT_WEIGHT_SEMI_BOLD, _textFace);
    if (FAILED(result))
    {
        result = CreateFace(L"Arial", DWRITE_FONT_WEIGHT_BOLD, _textFace);
    }
    if (FAILED(result))
    {
        Reset();
        return result;
    }
    _iconFontIsFallback = false;
    if (FAILED(CreateFace(kIconFamily, DWRITE_FONT_WEIGHT_NORMAL, _iconFace)) &&
        FAILED(CreateFace(kLegacyIconFamily, DWRITE_FONT_WEIGHT_NORMAL, _iconFace)))
    {
        _iconFace.reset();
        _iconFontIsFallback = true;
    }
    result = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(_wic.put()));
    if (FAILED(result))
    {
        Reset();
        return result;
    }
    return S_OK;
}

void FaceRenderer::Reset() noexcept
{
    _wic.reset();
    _iconFace.reset();
    _textFace.reset();
    _factory.reset();
    _dwriteModule.reset();
    _iconFontIsFallback = true;
}

bool FaceRenderer::Ready() const noexcept
{
    return _factory && _textFace && _wic;
}

bool FaceRenderer::HasIconFont() const noexcept
{
    return _iconFace && !_iconFontIsFallback;
}

HRESULT FaceRenderer::DrawRun(IDWriteFontFace* face, const uint16_t* glyphs, const float* advances, uint32_t count,
                              float emSize, float originX, float baselineY, uint32_t rgb, uint32_t* bgra) noexcept
{
    if (!face || !glyphs || !advances || count == 0 || !bgra)
    {
        return E_INVALIDARG;
    }
    DWRITE_GLYPH_RUN run{};
    run.fontFace = face;
    run.fontEmSize = emSize;
    run.glyphCount = count;
    run.glyphIndices = glyphs;
    run.glyphAdvances = advances;
    wil::com_ptr_nothrow<IDWriteGlyphRunAnalysis> analysis;
    HRESULT result =
        _factory->CreateGlyphRunAnalysis(&run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                         DWRITE_MEASURING_MODE_NATURAL, originX, baselineY, analysis.put());
    if (FAILED(result))
    {
        return result;
    }
    RECT bounds{};
    result = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
    if (FAILED(result))
    {
        return result;
    }
    bounds.left = std::max(bounds.left, 0L);
    bounds.top = std::max(bounds.top, 0L);
    bounds.right = std::min(bounds.right, static_cast<LONG>(kFaceSize));
    bounds.bottom = std::min(bounds.bottom, static_cast<LONG>(kFaceSize));
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    {
        return S_OK;
    }
    const uint32_t width = static_cast<uint32_t>(bounds.right - bounds.left);
    const uint32_t height = static_cast<uint32_t>(bounds.bottom - bounds.top);
    const uint32_t required = width * height * 3U;
    if (required > _glyphScratch.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    result = analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, _glyphScratch.data(), required);
    if (FAILED(result))
    {
        return result;
    }
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            const size_t source = (static_cast<size_t>(y) * width + x) * 3U;
            const uint32_t coverage = (static_cast<uint32_t>(_glyphScratch[source]) + _glyphScratch[source + 1U] +
                                       _glyphScratch[source + 2U] + 1U) /
                                      3U;
            const size_t destination =
                (static_cast<size_t>(bounds.top) + y) * kFaceSize + static_cast<size_t>(bounds.left) + x;
            BlendPixel(bgra[destination], rgb, coverage);
        }
    }
    return S_OK;
}

HRESULT FaceRenderer::DrawText(IDWriteFontFace* face, const wchar_t* text, uint32_t length, float emSize, float centerX,
                               float baselineY, float maxWidth, uint32_t rgb, uint32_t* bgra) noexcept
{
    if (!face || !text || length == 0)
    {
        return S_OK;
    }
    length = std::min(length, kMaximumFaceLabelCharacters);
    std::array<UINT32, kMaximumRunGlyphs> codePoints{};
    std::array<uint16_t, kMaximumRunGlyphs> glyphs{};
    std::array<float, kMaximumRunGlyphs> advances{};
    for (uint32_t index = 0; index < length; ++index)
    {
        codePoints[index] = static_cast<UINT32>(text[index]);
    }
    codePoints[length] = static_cast<UINT32>(kEllipsis);
    HRESULT result = face->GetGlyphIndices(codePoints.data(), length + 1, glyphs.data());
    if (FAILED(result))
    {
        return result;
    }
    DWRITE_FONT_METRICS metrics{};
    face->GetMetrics(&metrics);
    if (metrics.designUnitsPerEm == 0)
    {
        return E_UNEXPECTED;
    }
    std::array<DWRITE_GLYPH_METRICS, kMaximumRunGlyphs> glyphMetrics{};
    result = face->GetDesignGlyphMetrics(glyphs.data(), length + 1, glyphMetrics.data(), FALSE);
    if (FAILED(result))
    {
        return result;
    }
    const float scale = emSize / static_cast<float>(metrics.designUnitsPerEm);
    float total = 0.0f;
    for (uint32_t index = 0; index <= length; ++index)
    {
        advances[index] = static_cast<float>(glyphMetrics[index].advanceWidth) * scale;
        if (index < length)
        {
            total += advances[index];
        }
    }
    uint32_t drawn = length;
    bool ellipsized = false;
    if (total > maxWidth)
    {
        const float ellipsisWidth = advances[length];
        while (drawn > 0 && total + ellipsisWidth > maxWidth)
        {
            --drawn;
            total -= advances[drawn];
        }
        // Reuse the ellipsis glyph slot right after the kept prefix.
        glyphs[drawn] = glyphs[length];
        advances[drawn] = ellipsisWidth;
        total += ellipsisWidth;
        ellipsized = true;
    }
    const uint32_t count = ellipsized ? drawn + 1 : drawn;
    if (count == 0)
    {
        return S_OK;
    }
    return DrawRun(face, glyphs.data(), advances.data(), count, emSize, centerX - total * 0.5f, baselineY, rgb, bgra);
}

HRESULT FaceRenderer::ComposeKey(const FaceSpec& spec, uint32_t* bgra) noexcept
{
    if (!bgra)
    {
        return E_POINTER;
    }
    if (!Ready())
    {
        return E_UNEXPECTED;
    }
    const uint32_t background = spec.invalid ? 0x8B1A1A : spec.backgroundRgb;
    FillRect(bgra, 0, 0, static_cast<int>(kFaceSize), static_cast<int>(kFaceSize), background);
    if (spec.accentRing)
    {
        constexpr int ring = 4;
        FillRect(bgra, 0, 0, static_cast<int>(kFaceSize), ring, spec.accentRgb);
        FillRect(bgra, 0, static_cast<int>(kFaceSize) - ring, static_cast<int>(kFaceSize), static_cast<int>(kFaceSize),
                 spec.accentRgb);
        FillRect(bgra, 0, 0, ring, static_cast<int>(kFaceSize), spec.accentRgb);
        FillRect(bgra, static_cast<int>(kFaceSize) - ring, 0, static_cast<int>(kFaceSize), static_cast<int>(kFaceSize),
                 spec.accentRgb);
    }

    const bool hasLabel = spec.label && spec.labelLength != 0;
    const float centerX = kFaceSizeF * 0.5f;
    // With a label the picture sits in the upper two thirds; without one it is centered.
    const float pictureCenterY = hasLabel ? 46.0f : kFaceSizeF * 0.5f;
    HRESULT result = S_OK;

    if (spec.invalid)
    {
        const wchar_t mark[] = L"!";
        result = DrawText(_textFace.get(), mark, 1, kBigEm, centerX, pictureCenterY + kBigEm * 0.36f, kLabelMaxWidth,
                          0xFFFFFF, bgra);
    }
    else if (spec.image && spec.imageWidth != 0 && spec.imageHeight != 0)
    {
        const int left = static_cast<int>(centerX) - static_cast<int>(spec.imageWidth) / 2;
        const int top = static_cast<int>(pictureCenterY) - static_cast<int>(spec.imageHeight) / 2;
        for (uint32_t y = 0; y < spec.imageHeight; ++y)
        {
            const int destinationY = top + static_cast<int>(y);
            if (destinationY < 0 || destinationY >= static_cast<int>(kFaceSize))
            {
                continue;
            }
            for (uint32_t x = 0; x < spec.imageWidth; ++x)
            {
                const int destinationX = left + static_cast<int>(x);
                if (destinationX < 0 || destinationX >= static_cast<int>(kFaceSize))
                {
                    continue;
                }
                BlendPremultiplied(
                    bgra[static_cast<size_t>(destinationY) * kFaceSize + static_cast<size_t>(destinationX)],
                    spec.image[static_cast<size_t>(y) * spec.imageWidth + x]);
            }
        }
    }
    else if (spec.big && spec.bigLength != 0)
    {
        const float em = spec.bigLength > 5 ? kBigEm * 0.72f : kBigEm;
        result = DrawText(_textFace.get(), spec.big, std::min(spec.bigLength, kMaximumFaceBigCharacters), em, centerX,
                          pictureCenterY + em * 0.36f, kLabelMaxWidth, spec.foregroundRgb, bgra);
    }
    else if (spec.icon != 0 && _iconFace)
    {
        // Icon glyphs are drawn on the em box; the baseline sits about 0.9 em below the box top.
        result = DrawText(_iconFace.get(), &spec.icon, 1, kIconEm, centerX, pictureCenterY + kIconEm * 0.42f,
                          kFaceSizeF, spec.foregroundRgb, bgra);
    }
    if (FAILED(result))
    {
        return result;
    }
    if (hasLabel)
    {
        result = DrawText(_textFace.get(), spec.label, std::min(spec.labelLength, kMaximumFaceLabelCharacters),
                          kLabelEm, centerX, 100.0f, kLabelMaxWidth, spec.foregroundRgb, bgra);
    }
    return result;
}

HRESULT FaceRenderer::EncodeJpeg(const uint32_t* bgra, uint32_t width, uint32_t height, uint32_t stridePixels,
                                 uint8_t* out, uint32_t capacity, uint32_t& bytes) noexcept
{
    bytes = 0;
    if (!bgra || !out)
    {
        return E_POINTER;
    }
    if (width == 0 || height == 0 || width > kMaximumEncodeWidth || stridePixels < width || capacity == 0)
    {
        return E_INVALIDARG;
    }
    if (!_wic)
    {
        return E_UNEXPECTED;
    }
    wil::com_ptr_nothrow<IWICStream> stream;
    HRESULT result = _wic->CreateStream(stream.put());
    if (SUCCEEDED(result))
    {
        result = stream->InitializeFromMemory(out, capacity);
    }
    wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(result))
    {
        result = _wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.put());
    }
    if (SUCCEEDED(result))
    {
        result = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
    }
    wil::com_ptr_nothrow<IWICBitmapFrameEncode> frame;
    wil::com_ptr_nothrow<IPropertyBag2> properties;
    if (SUCCEEDED(result))
    {
        result = encoder->CreateNewFrame(frame.put(), properties.put());
    }
    if (SUCCEEDED(result) && properties)
    {
        PROPBAG2 quality{};
        wchar_t qualityName[] = L"ImageQuality";
        quality.pstrName = qualityName;
        VARIANT qualityValue{};
        qualityValue.vt = VT_R4;
        qualityValue.fltVal = 0.80f;
        (void)properties->Write(1, &quality, &qualityValue);
        PROPBAG2 subsampling{};
        wchar_t subsamplingName[] = L"JpegYCrCbSubsampling";
        subsampling.pstrName = subsamplingName;
        VARIANT subsamplingValue{};
        subsamplingValue.vt = VT_UI1;
        subsamplingValue.bVal = WICJpegYCrCbSubsampling444;
        (void)properties->Write(1, &subsampling, &subsamplingValue);
    }
    if (SUCCEEDED(result))
    {
        result = frame->Initialize(properties.get());
    }
    if (SUCCEEDED(result))
    {
        result = frame->SetSize(width, height);
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(result))
    {
        result = frame->SetPixelFormat(&format);
    }
    if (SUCCEEDED(result) && format != GUID_WICPixelFormat24bppBGR)
    {
        result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    for (uint32_t y = 0; SUCCEEDED(result) && y < height; ++y)
    {
        const uint32_t* row = bgra + static_cast<size_t>(y) * stridePixels;
        for (uint32_t x = 0; x < width; ++x)
        {
            _rowScratch[static_cast<size_t>(x) * 3U] = static_cast<uint8_t>(row[x] & 0xFFU);
            _rowScratch[static_cast<size_t>(x) * 3U + 1U] = static_cast<uint8_t>((row[x] >> 8U) & 0xFFU);
            _rowScratch[static_cast<size_t>(x) * 3U + 2U] = static_cast<uint8_t>((row[x] >> 16U) & 0xFFU);
        }
        result = frame->WritePixels(1, width * 3U, width * 3U, _rowScratch.data());
    }
    if (SUCCEEDED(result))
    {
        result = frame->Commit();
    }
    if (SUCCEEDED(result))
    {
        result = encoder->Commit();
    }
    if (FAILED(result))
    {
        return result;
    }
    ULARGE_INTEGER position{};
    LARGE_INTEGER zero{};
    result = stream->Seek(zero, STREAM_SEEK_CUR, &position);
    if (FAILED(result))
    {
        return result;
    }
    if (position.QuadPart == 0 || position.QuadPart > capacity)
    {
        return STG_E_MEDIUMFULL;
    }
    bytes = static_cast<uint32_t>(position.QuadPart);
    return S_OK;
}

HRESULT FaceRenderer::DecodeImage(const wchar_t* path, uint32_t maxSize, uint32_t* bgra, uint32_t capacityPixels,
                                  uint32_t& width, uint32_t& height) noexcept
{
    width = 0;
    height = 0;
    if (!path || !bgra)
    {
        return E_POINTER;
    }
    if (maxSize == 0 || capacityPixels == 0)
    {
        return E_INVALIDARG;
    }
    if (!_wic)
    {
        return E_UNEXPECTED;
    }
    wil::com_ptr_nothrow<IWICBitmapDecoder> decoder;
    HRESULT result =
        _wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, frame.put());
    if (FAILED(result))
    {
        return result;
    }
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    result = frame->GetSize(&sourceWidth, &sourceHeight);
    if (FAILED(result) || sourceWidth == 0 || sourceHeight == 0)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    UINT targetWidth = sourceWidth;
    UINT targetHeight = sourceHeight;
    if (sourceWidth > maxSize || sourceHeight > maxSize)
    {
        const double scale =
            std::min(static_cast<double>(maxSize) / sourceWidth, static_cast<double>(maxSize) / sourceHeight);
        targetWidth = std::max(1U, static_cast<UINT>(std::lround(sourceWidth * scale)));
        targetHeight = std::max(1U, static_cast<UINT>(std::lround(sourceHeight * scale)));
    }
    if (static_cast<uint64_t>(targetWidth) * targetHeight > capacityPixels)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    wil::com_ptr_nothrow<IWICBitmapSource> source;
    if (targetWidth != sourceWidth || targetHeight != sourceHeight)
    {
        wil::com_ptr_nothrow<IWICBitmapScaler> scaler;
        result = _wic->CreateBitmapScaler(scaler.put());
        if (SUCCEEDED(result))
        {
            result = scaler->Initialize(frame.get(), targetWidth, targetHeight, WICBitmapInterpolationModeFant);
        }
        if (FAILED(result))
        {
            return result;
        }
        result = scaler.query_to(source.put());
    }
    else
    {
        result = frame.query_to(source.put());
    }
    wil::com_ptr_nothrow<IWICFormatConverter> converter;
    result = _wic->CreateFormatConverter(converter.put());
    if (SUCCEEDED(result))
    {
        result = converter->Initialize(source.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                       0.0, WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(result))
    {
        result = converter->CopyPixels(nullptr, targetWidth * 4U, targetWidth * targetHeight * 4U,
                                       reinterpret_cast<BYTE*>(bgra));
    }
    if (FAILED(result))
    {
        return result;
    }
    width = targetWidth;
    height = targetHeight;
    return S_OK;
}
} // namespace Logicon
