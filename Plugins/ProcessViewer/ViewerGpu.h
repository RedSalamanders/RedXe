#pragma once

#include <array>
#include <cstdint>
#include <d3d11.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

inline constexpr uint32_t kViewerMaximumQuads = 1024;
inline constexpr uint32_t kViewerAtlasSize = 1024;
inline constexpr uint32_t kViewerGlyphCell = 48;
inline constexpr uint32_t kViewerGlyphColumns = kViewerAtlasSize / kViewerGlyphCell;
inline constexpr uint32_t kViewerGlyphCapacity = kViewerGlyphColumns * kViewerGlyphColumns;

enum ViewerQuadKind : uint32_t
{
    ViewerQuadKindFill = 0,
    ViewerQuadKindStroke = 1,
    ViewerQuadKindRing = 2,
    ViewerQuadKindGlyph = 3,
    ViewerQuadKindGlow = 4,
};

struct ViewerQuadInstance final
{
    float rect[4];
    float color[4];
    float uv[4];
    float params[4];
};

class ViewerDrawList final
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

    [[nodiscard]] const ViewerQuadInstance* Data() const noexcept
    {
        return _items.data();
    }

    [[nodiscard]] bool Add(ViewerQuadKind kind, float x, float y, float width, float height, float red, float green,
                           float blue, float alpha, float radius, float extra, float u0, float v0, float u1,
                           float v1) noexcept
    {
        if (_count >= _items.size() || width <= 0.0f || height <= 0.0f || alpha <= 0.0f)
        {
            return _count < _items.size();
        }
        ViewerQuadInstance& item = _items[_count++];
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
        return Add(ViewerQuadKindFill, x, y, width, height, red, green, blue, alpha, radius, 0.0f, 0.0f, 0.0f, 0.0f,
                   0.0f);
    }

    [[nodiscard]] bool AddStroke(float x, float y, float width, float height, float red, float green, float blue,
                                 float alpha, float radius, float stroke) noexcept
    {
        return Add(ViewerQuadKindStroke, x, y, width, height, red, green, blue, alpha, radius, stroke, 0.0f, 0.0f, 0.0f,
                   0.0f);
    }

    [[nodiscard]] bool AddGlow(float x, float y, float width, float height, float red, float green, float blue,
                               float alpha, float radius) noexcept
    {
        return Add(ViewerQuadKindGlow, x, y, width, height, red, green, blue, alpha, radius, 0.0f, 0.0f, 0.0f, 0.0f,
                   0.0f);
    }

    [[nodiscard]] bool AddRing(float x, float y, float width, float height, float red, float green, float blue,
                               float alpha, float innerRadius) noexcept
    {
        return Add(ViewerQuadKindRing, x, y, width, height, red, green, blue, alpha, 0.0f, innerRadius, 0.0f, 0.0f,
                   0.0f, 0.0f);
    }

  private:
    std::array<ViewerQuadInstance, kViewerMaximumQuads> _items{};
    uint32_t _count = 0;
};

class ViewerGpuResources final
{
  public:
    ViewerGpuResources() = default;
    ~ViewerGpuResources();

    ViewerGpuResources(const ViewerGpuResources&) = delete;
    ViewerGpuResources& operator=(const ViewerGpuResources&) = delete;

    [[nodiscard]] HRESULT Initialize(ID3D11Device* device) noexcept;
    void Reset() noexcept;
    [[nodiscard]] HRESULT EnsureGlyphs(const wchar_t* text, uint32_t characters) noexcept;
    [[nodiscard]] float MeasureText(const wchar_t* text, uint32_t characters, float height) const noexcept;
    [[nodiscard]] HRESULT AppendText(ViewerDrawList& list, float x, float y, float height, float red, float green,
                                     float blue, float alpha, const wchar_t* text, uint32_t characters) noexcept;
    [[nodiscard]] HRESULT Render(ID3D11DeviceContext* context, float width, float height,
                                 const ViewerDrawList& list) noexcept;

  private:
    struct ViewerConstants final
    {
        float targetSize[4];
    };

    [[nodiscard]] HRESULT BuildStaticAtlas() noexcept;
    [[nodiscard]] uint32_t FindGlyph(wchar_t character) const noexcept;
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
    std::array<uint8_t, kViewerAtlasSize * kViewerAtlasSize> _atlasPixels{};
    std::array<wchar_t, kViewerGlyphCapacity> _glyphCharacters{};
    std::array<float, kViewerGlyphCapacity> _glyphAdvances{};
    uint32_t _glyphCount = 0;
    uint32_t _dynamicCursor = 0;
    bool _atlasDirty = false;
};

[[nodiscard]] HRESULT ViewerGpuAcquire(ID3D11Device* device) noexcept;
void ViewerGpuRelease() noexcept;
// Worker callers hold ViewerGpuLock across the instance ownership check, this lookup, and resource use.
[[nodiscard]] ViewerGpuResources* ViewerGpuGet() noexcept;
void ViewerGpuLock() noexcept;
void ViewerGpuUnlock() noexcept;
