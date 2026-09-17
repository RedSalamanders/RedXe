#pragma once

// Key-face rendering for the keypad: CPU composition of one 118×118 BGRA face (background, Segoe Fluent icon or
// PNG, label, large text, accent ring), and WIC JPEG encoding of a composed surface. Everything works on bounded
// caller buffers; the renderer owns only DirectWrite/WIC factories, font faces, and one glyph scratch. It runs on
// the device lane, never on the UI thread.

#include <array>
#include <cstddef>
#include <cstdint>
#include <dwrite.h>
#include <wincodec.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace Logicon
{
inline constexpr uint32_t kFaceSize = 118;
inline constexpr uint32_t kFacePixels = kFaceSize * kFaceSize;
inline constexpr uint32_t kMaximumFaceLabelCharacters = 16;
inline constexpr uint32_t kMaximumFaceBigCharacters = 8;
inline constexpr uint32_t kFaceIconPixels = 72;
inline constexpr uint32_t kMaximumJpegBytes = 128U * 1024U;
// Widest surface EncodeJpeg accepts: the whole 480-pixel panel.
inline constexpr uint32_t kMaximumEncodeWidth = 480;

// Everything a face shows. Strings are borrowed for the compose call.
struct FaceSpec final
{
    uint32_t backgroundRgb = 0x000000;
    uint32_t foregroundRgb = 0xFFFFFF;
    uint32_t accentRgb = 0x2F80ED;
    bool accentRing = false;
    bool invalid = false;
    wchar_t icon = 0;
    const wchar_t* label = nullptr;
    uint32_t labelLength = 0;
    const wchar_t* big = nullptr;
    uint32_t bigLength = 0;
    // Optional pre-decoded PNG icon (premultiplied BGRA), centered where the glyph icon would go.
    const uint32_t* image = nullptr;
    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;
};

// Segoe Fluent Icons glyph names accepted in settings ("icon": "ChevronRight"). Returns 0 for an unknown name.
[[nodiscard]] wchar_t FluentGlyphFromName(const char* name, uint32_t bytes) noexcept;
[[nodiscard]] uint32_t FluentGlyphNameCount() noexcept;
[[nodiscard]] const char* FluentGlyphNameAt(uint32_t index) noexcept;

class FaceRenderer final
{
  public:
    FaceRenderer() = default;
    ~FaceRenderer();
    FaceRenderer(const FaceRenderer&) = delete;
    FaceRenderer& operator=(const FaceRenderer&) = delete;

    // Creates the DirectWrite factory, the text and icon faces, and the WIC factory. Idempotent.
    [[nodiscard]] HRESULT Initialize() noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool Ready() const noexcept;
    [[nodiscard]] bool HasIconFont() const noexcept;

    // Composes one face into bgra (kFacePixels entries, row-major, opaque 0xFFRRGGBB in memory order B,G,R,A).
    [[nodiscard]] HRESULT ComposeKey(const FaceSpec& spec, uint32_t* bgra) noexcept;

    // Encodes an opaque BGRA surface as a baseline JPEG (quality 80, 4:4:4) into out. STG_E_MEDIUMFULL when the
    // image does not fit capacity.
    [[nodiscard]] HRESULT EncodeJpeg(const uint32_t* bgra, uint32_t width, uint32_t height, uint32_t stridePixels,
                                     uint8_t* out, uint32_t capacity, uint32_t& bytes) noexcept;

    // Decodes an image file to premultiplied BGRA scaled to fit maxSize × maxSize. Fails when the scaled image
    // exceeds capacityPixels.
    [[nodiscard]] HRESULT DecodeImage(const wchar_t* path, uint32_t maxSize, uint32_t* bgra, uint32_t capacityPixels,
                                      uint32_t& width, uint32_t& height) noexcept;

  private:
    [[nodiscard]] HRESULT CreateFace(const wchar_t* family, DWRITE_FONT_WEIGHT weight,
                                     wil::com_ptr_nothrow<IDWriteFontFace>& face) noexcept;
    // Draws text with one face into the 118×118 surface, centered horizontally at centerX with its baseline at
    // baselineY, clipped to the surface. Text wider than maxWidth is ellipsized.
    [[nodiscard]] HRESULT DrawText(IDWriteFontFace* face, const wchar_t* text, uint32_t length, float emSize,
                                   float centerX, float baselineY, float maxWidth, uint32_t rgb,
                                   uint32_t* bgra) noexcept;
    [[nodiscard]] HRESULT DrawRun(IDWriteFontFace* face, const uint16_t* glyphs, const float* advances, uint32_t count,
                                  float emSize, float originX, float baselineY, uint32_t rgb, uint32_t* bgra) noexcept;

    wil::unique_hmodule _dwriteModule;
    wil::com_ptr_nothrow<IDWriteFactory> _factory;
    wil::com_ptr_nothrow<IDWriteFontFace> _textFace;
    wil::com_ptr_nothrow<IDWriteFontFace> _iconFace;
    wil::com_ptr_nothrow<IWICImagingFactory> _wic;
    bool _iconFontIsFallback = true;
    std::array<uint8_t, kFacePixels * 3> _glyphScratch{};
    std::array<uint8_t, kMaximumEncodeWidth * 3> _rowScratch{};
};
} // namespace Logicon
