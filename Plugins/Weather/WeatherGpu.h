#pragma once

#include <array>
#include <cstdint>
#include <d3d11.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

inline constexpr uint32_t kWeatherMaximumQuads = 1024;
inline constexpr uint32_t kWeatherAtlasSize = 1024;
inline constexpr uint32_t kWeatherGlyphCell = 48;
inline constexpr uint32_t kWeatherIconCell = 96;
inline constexpr uint32_t kWeatherGlyphColumns = kWeatherAtlasSize / kWeatherGlyphCell;
inline constexpr uint32_t kWeatherGlyphCapacity = kWeatherGlyphColumns * kWeatherGlyphColumns;
// Header temperature glyphs (WeatherFormatTemperature: %.0f digits, sign, degree, C/F). Each also gets a
// kWeatherIconCell twin so the hero row draws as sharp as the condition icon beside it.
inline constexpr wchar_t kWeatherHeroGlyphs[] = {L'0', L'1', L'2', L'3', L'4',   L'5', L'6',
                                                 L'7', L'8', L'9', L'-', 0x00B0, L'C', L'F'};

enum WeatherQuadKind : uint32_t
{
    WeatherQuadKindFill = 0,
    WeatherQuadKindStroke = 1,
    WeatherQuadKindRing = 2,
    WeatherQuadKindGlyph = 3,
    WeatherQuadKindGlow = 4,
};

struct WeatherQuadInstance final
{
    float rect[4];
    float color[4];
    float uv[4];
    float params[4];
};

struct WeatherGlyphInk final
{
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

class WeatherDrawList final
{
  public:
    void Reset() noexcept
    {
        _count = 0;
    }

    [[nodiscard]] uint32_t Count() const noexcept
    {
        return _count;
    }

    [[nodiscard]] const WeatherQuadInstance* Data() const noexcept
    {
        return _items.data();
    }

    [[nodiscard]] bool Add(WeatherQuadKind kind, float x, float y, float width, float height, float red, float green,
                           float blue, float alpha, float radius, float extra, float u0, float v0, float u1,
                           float v1) noexcept
    {
        if (_count >= _items.size() || width <= 0.0f || height <= 0.0f || alpha <= 0.0f)
        {
            return _count < _items.size();
        }
        WeatherQuadInstance& item = _items[_count++];
        item.rect[0] = x;
        item.rect[1] = y;
        item.rect[2] = width;
        item.rect[3] = height;
        item.color[0] = red;
        item.color[1] = green;
        item.color[2] = blue;
        item.color[3] = alpha;
        item.uv[0] = u0;
        item.uv[1] = v0;
        item.uv[2] = u1;
        item.uv[3] = v1;
        item.params[0] = static_cast<float>(kind);
        item.params[1] = radius;
        item.params[2] = extra;
        item.params[3] = 0.0f;
        return true;
    }

    [[nodiscard]] bool AddFill(float x, float y, float width, float height, float red, float green, float blue,
                               float alpha, float radius) noexcept
    {
        return Add(WeatherQuadKindFill, x, y, width, height, red, green, blue, alpha, radius, 0.0f, 0.0f, 0.0f, 0.0f,
                   0.0f);
    }

    [[nodiscard]] bool AddStroke(float x, float y, float width, float height, float red, float green, float blue,
                                 float alpha, float radius, float stroke) noexcept
    {
        return Add(WeatherQuadKindStroke, x, y, width, height, red, green, blue, alpha, radius, stroke, 0.0f, 0.0f,
                   0.0f, 0.0f);
    }

    [[nodiscard]] bool AddGlow(float x, float y, float width, float height, float red, float green, float blue,
                               float alpha, float radius) noexcept
    {
        return Add(WeatherQuadKindGlow, x, y, width, height, red, green, blue, alpha, radius, 0.0f, 0.0f, 0.0f, 0.0f,
                   0.0f);
    }

    [[nodiscard]] bool AddRing(float x, float y, float width, float height, float red, float green, float blue,
                               float alpha, float innerRadius) noexcept
    {
        return Add(WeatherQuadKindRing, x, y, width, height, red, green, blue, alpha, 0.0f, innerRadius, 0.0f, 0.0f,
                   0.0f, 0.0f);
    }

  private:
    std::array<WeatherQuadInstance, kWeatherMaximumQuads> _items{};
    uint32_t _count = 0;
};

class WeatherGpuResources final
{
  public:
    WeatherGpuResources() = default;
    ~WeatherGpuResources();

    WeatherGpuResources(const WeatherGpuResources&) = delete;
    WeatherGpuResources& operator=(const WeatherGpuResources&) = delete;

    [[nodiscard]] HRESULT Initialize(ID3D11Device* device) noexcept;
    void Reset() noexcept;
    [[nodiscard]] HRESULT EnsureGlyphs(const wchar_t* text, uint32_t characters) noexcept;
    [[nodiscard]] float MeasureText(const wchar_t* text, uint32_t characters, float height) const noexcept;
    [[nodiscard]] HRESULT AppendText(WeatherDrawList& list, float x, float y, float height, float red, float green,
                                     float blue, float alpha, const wchar_t* text, uint32_t characters) noexcept;
    [[nodiscard]] HRESULT AppendIcon(WeatherDrawList& list, float x, float y, float size, wchar_t glyph, float red,
                                     float green, float blue, float alpha) noexcept;
    [[nodiscard]] HRESULT AppendIconFit(WeatherDrawList& list, float x, float y, float width, float height,
                                        wchar_t glyph, float red, float green, float blue, float alpha) noexcept;
    [[nodiscard]] HRESULT Render(ID3D11DeviceContext* context, float width, float height,
                                 const WeatherDrawList& list) noexcept;

  private:
    struct WeatherConstants final
    {
        float targetSize[4];
    };

    [[nodiscard]] HRESULT BuildStaticAtlas() noexcept;
    [[nodiscard]] uint32_t FindGlyph(wchar_t character) const noexcept;
    // Returns the 96 px twin of a glyph slot when the drawn extent exceeds ~1.1x its 48 px cell, else the slot.
    [[nodiscard]] uint32_t ResolveLargeSlot(uint32_t slot, float extent) const noexcept;
    void UploadAtlas() noexcept;

    ID3D11Device* _deviceIdentity = nullptr;
    wil::com_ptr_nothrow<ID3D11VertexShader> _vertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _pixelShader;
    wil::com_ptr_nothrow<ID3D11InputLayout> _inputLayout;
    wil::com_ptr_nothrow<ID3D11Buffer> _constantBuffer;
    wil::com_ptr_nothrow<ID3D11Buffer> _instanceBuffer;
    wil::com_ptr_nothrow<ID3D11Texture2D> _atlas;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _atlasView;
    wil::com_ptr_nothrow<ID3D11SamplerState> _sampler;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _rasterizer;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _depthState;
    wil::com_ptr_nothrow<ID3D11BlendState> _blendState;
    std::array<uint8_t, kWeatherAtlasSize * kWeatherAtlasSize> _atlasPixels{};
    std::array<wchar_t, kWeatherGlyphCapacity> _glyphCharacters{};
    std::array<float, kWeatherGlyphCapacity> _glyphAdvances{};
    std::array<WeatherGlyphInk, kWeatherGlyphCapacity> _glyphInk{};
    std::array<uint16_t, kWeatherGlyphCapacity> _glyphAtlasX{};
    std::array<uint16_t, kWeatherGlyphCapacity> _glyphAtlasY{};
    std::array<uint16_t, kWeatherGlyphCapacity> _glyphCell{};
    std::array<uint16_t, kWeatherGlyphCapacity> _glyphLarge{};
    uint32_t _glyphCount = 0;
    uint32_t _staticGlyphCount = 0;
    uint32_t _dynamicCursor = 0;
    bool _atlasDirty = false;
};

[[nodiscard]] HRESULT WeatherGpuAcquire(ID3D11Device* device) noexcept;
void WeatherGpuRelease() noexcept;
// Hero glyphs whose kWeatherIconCell twin linked in the shared atlas; test diagnostics only.
[[nodiscard]] uint32_t WeatherGpuHeroTwinCount() noexcept;
// Worker callers hold WeatherGpuLock across the instance ownership check, this lookup, and resource use.
[[nodiscard]] WeatherGpuResources* WeatherGpuGet() noexcept;
void WeatherGpuLock() noexcept;
void WeatherGpuUnlock() noexcept;
