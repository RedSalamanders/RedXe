#pragma once

#include "FluentIcons.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

// Host chrome: the mouse edge bands and the raise overlay (dim, shadow, close control) drawn by the renderer into the
// swap chain. It replaces the former layered chrome HWNDs and their GDI paint, so a shipped page owns no child window
// over the swap chain and can present with independent flip. Every state change costs one coalesced frame and nothing
// else: no timer, no continuous frame, no window operation.

struct HostChromeEdgeBand final
{
    RECT rect{};
    int direction = 0;
    bool revealed = false;

    bool operator==(const HostChromeEdgeBand&) const noexcept = default;
};

struct HostChromeState final
{
    bool raised = false;
    RECT content{};
    RECT close{};
    RECT shadow{};
    // Dim wash alpha over everything except the raised content, 0..255 (kRaiseOverlayDimAlpha when settled).
    BYTE dimAlpha = 0;
    bool closeHovered = false;
    std::array<HostChromeEdgeBand, 2> bands{};

    bool operator==(const HostChromeState&) const noexcept = default;
};

// Glyph slots in the host atlas, in atlas order.
enum class HostChromeGlyph : uint32_t
{
    ChevronLeft = 0,
    ChevronRight = 1,
    Close = 2,
    Count = 3,
};

// Dim strips: the parts of the client the raise dim covers, i.e. everything outside the content rectangle, as at
// most four rectangles (left, right, top, bottom). Empty strips are omitted. Pure so tests can cover it.
[[nodiscard]] inline size_t HostChromeDimStrips(UINT clientWidth, UINT clientHeight, const RECT& content, RECT* strips,
                                                size_t capacity) noexcept
{
    if (!strips || capacity < 4 || clientWidth == 0 || clientHeight == 0)
    {
        return 0;
    }
    const LONG width = static_cast<LONG>(clientWidth);
    const LONG height = static_cast<LONG>(clientHeight);
    const LONG left = content.right > content.left ? content.left : 0;
    const LONG right = content.right > content.left ? content.right : 0;
    const LONG top = content.bottom > content.top ? content.top : 0;
    const LONG bottom = content.bottom > content.top ? content.bottom : 0;
    size_t count = 0;
    if (left > 0)
    {
        strips[count++] = RECT{0, 0, left, height};
    }
    if (right < width)
    {
        strips[count++] = RECT{right, 0, width, height};
    }
    if (top > 0)
    {
        strips[count++] = RECT{left, 0, right, top};
    }
    if (bottom < height)
    {
        strips[count++] = RECT{left, bottom, right, height};
    }
    return count;
}

// Number of quads one chrome state draws, for the renderer's per-frame counter and for tests.
[[nodiscard]] inline size_t HostChromeQuadCount(const HostChromeState& state, UINT clientWidth, UINT clientHeight,
                                                bool glyphsAvailable) noexcept
{
    size_t count = 0;
    if (state.raised && state.dimAlpha != 0)
    {
        std::array<RECT, 4> strips{};
        count += HostChromeDimStrips(clientWidth, clientHeight, state.content, strips.data(), strips.size());
    }
    if (state.raised && state.shadow.right > state.shadow.left && state.shadow.bottom > state.shadow.top)
    {
        ++count;
    }
    if (state.raised && state.close.right > state.close.left && state.close.bottom > state.close.top)
    {
        if (state.closeHovered)
        {
            ++count;
        }
        if (glyphsAvailable)
        {
            ++count;
        }
    }
    for (const HostChromeEdgeBand& band : state.bands)
    {
        if (band.revealed && band.rect.right > band.rect.left && band.rect.bottom > band.rect.top)
        {
            count += glyphsAvailable ? 2 : 1;
        }
    }
    return count;
}

enum class HostChromePhase
{
    // Dim strips and shadow: drawn after the tiles and before the raised widget, so plugin pixels stay undimmed.
    BelowRaised,
    // Close control and edge bands: drawn after the raised widget.
    AboveRaised,
};

class HostChromeResources final
{
  public:
    HostChromeResources() = default;
    ~HostChromeResources();

    HostChromeResources(const HostChromeResources&) = delete;
    HostChromeResources& operator=(const HostChromeResources&) = delete;
    HostChromeResources(HostChromeResources&&) = delete;
    HostChromeResources& operator=(HostChromeResources&&) = delete;

    // Creates the pipeline objects and rasterizes the glyph atlas for `dpi`. A missing DirectWrite face leaves the
    // pipeline usable for washes only.
    HRESULT Initialize(ID3D11Device* device, UINT dpi) noexcept;
    void Release() noexcept;
    // Re-rasterizes the atlas when the DPI actually changed; never per frame.
    HRESULT SetDpi(UINT dpi) noexcept;
    // Draws the quads of one phase. `quadsDrawn` accumulates the count for the frame.
    HRESULT Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, UINT viewportWidth, UINT viewportHeight,
                 const HostChromeState& state, HostChromePhase phase, size_t& quadsDrawn) noexcept;

    [[nodiscard]] bool Ready() const noexcept;
    [[nodiscard]] bool GlyphsAvailable() const noexcept;
    [[nodiscard]] FluentIcons::IconFont Font() const noexcept;
    [[nodiscard]] UINT Dpi() const noexcept;

  private:
    struct Constants final
    {
        float rect[4];
        float color[4];
        float uv[4];
        float viewportGlyph[4];
    };
    static_assert(sizeof(Constants) == 64);

    HRESULT BuildAtlas(UINT dpi) noexcept;
    HRESULT DrawQuad(ID3D11DeviceContext* context, const RECT& rect, const float (&color)[4], UINT viewportWidth,
                     UINT viewportHeight, const HostChromeGlyph* glyph, size_t& quadsDrawn) noexcept;

    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11VertexShader> _vertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _pixelShader;
    wil::com_ptr_nothrow<ID3D11Buffer> _constants;
    wil::com_ptr_nothrow<ID3D11BlendState> _blend;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _rasterizer;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _depth;
    wil::com_ptr_nothrow<ID3D11SamplerState> _sampler;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _atlasView;
    // Glyph cell size in atlas pixels and the ink bounds of each glyph inside its cell (pixels), for exact centring.
    static constexpr uint32_t kCellPixels = 96;
    static constexpr uint32_t kGlyphCount = static_cast<uint32_t>(HostChromeGlyph::Count);
    std::array<RECT, kGlyphCount> _glyphInk{};
    FluentIcons::IconFont _font = FluentIcons::IconFont::TextFallback;
    UINT _dpi = 0;
    bool _glyphsAvailable = false;
    bool _atlasFailureLogged = false;
};
