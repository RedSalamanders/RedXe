#include "Launcher.Tests.GlyphBatch.h"

#include "../../Common/Actions/FluentGlyphNames.h"
#include "../../Common/Actions/GlyphIcon.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <ole2.h>

#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <crtdbg.h>
namespace
{
thread_local bool gRejectNextAllocation = false;
int __cdecl RejectNextAllocation(int type, void*, size_t, int, long, const unsigned char*, int)
{
    if (type == _HOOK_ALLOC && gRejectNextAllocation)
    {
        gRejectNextAllocation = false;
        return FALSE;
    }
    return TRUE;
}
} // namespace
#endif

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

HRESULT ValidateLauncherGlyphRasterization(const wchar_t* pixelsPath) noexcept
{
    const HRESULT initialized = OleInitialize(nullptr);
    if (FAILED(initialized))
        return initialized;
    const auto uninitialize = wil::scope_exit([]() noexcept { OleUninitialize(); });
    FILE* pixels = nullptr;
    if (pixelsPath && (_wfopen_s(&pixels, pixelsPath, L"wb") != 0 || !pixels))
        return E_FAIL;
    const auto close = wil::scope_exit(
        [&]() noexcept
        {
            if (pixels)
                (void)std::fclose(pixels);
        });
    constexpr size_t capacity = 512 * 512;
    constexpr uint32_t sentinel = 0x13579BDF;
    std::unique_ptr<uint32_t[]> reference(new (std::nothrow) uint32_t[capacity + 2]);
    std::unique_ptr<uint32_t[]> candidate(new (std::nothrow) uint32_t[capacity + 2]);
    if (!reference || !candidate)
        return E_OUTOFMEMORY;
    RedXeActions::FluentGlyphRasterizer batch;
    if (RedXeActions::RasterizeFluentGlyph(0, 24, reference.get()) != E_INVALIDARG ||
        RedXeActions::RasterizeFluentGlyph(L'\xE713', 0, reference.get()) != E_INVALIDARG ||
        RedXeActions::RasterizeFluentGlyph(L'\xE713', 513, reference.get()) != E_INVALIDARG ||
        RedXeActions::RasterizeFluentGlyph(L'\xE713', 24, nullptr) != E_INVALIDARG ||
        RedXeActions::RasterizeFluentGlyph(L'\xFFFF', 24, reference.get()) != HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
        return E_FAIL;
    if (batch.Rasterize(0, 24, candidate.get()) != E_INVALIDARG ||
        batch.Rasterize(L'\xE713', 0, candidate.get()) != E_INVALIDARG ||
        batch.Rasterize(L'\xE713', 513, candidate.get()) != E_INVALIDARG ||
        batch.Rasterize(L'\xE713', 24, nullptr) != E_INVALIDARG ||
        batch.Rasterize(L'\xFFFF', 24, candidate.get()) != HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
        return E_FAIL;
    uint32_t frames = 0;
    for (const uint32_t edge : {1U, 24U, 48U, 256U, 512U})
    {
        const size_t used = static_cast<size_t>(edge) * edge;
        for (const auto& named : RedXeActions::kFluentGlyphs)
        {
            std::fill_n(reference.get(), used + 2, sentinel);
            const HRESULT result = RedXeActions::RasterizeFluentGlyph(named.glyph, edge, reference.get() + 1);
            if (FAILED(result) || reference[0] != sentinel || reference[used + 1] != sentinel)
                return FAILED(result) ? result : E_FAIL;
            std::fill_n(candidate.get(), used + 2, sentinel);
            if (batch.Rasterize(named.glyph, edge, candidate.get() + 1) != result ||
                std::memcmp(reference.get(), candidate.get(), (used + 2) * sizeof(uint32_t)) != 0)
                return E_FAIL;
            uint64_t hash = 14695981039346656037ULL;
            for (size_t index = 1; index <= used; ++index)
            {
                const uint32_t value = reference[index];
                const uint32_t alpha = value >> 24;
                if (value != ((alpha << 24) | (alpha << 16) | (alpha << 8) | alpha))
                    return E_FAIL;
                for (uint32_t byte = 0; byte < 4; ++byte)
                    hash = (hash ^ ((value >> (byte * 8)) & 255)) * 1099511628211ULL;
            }
            if (pixels &&
                std::fprintf(pixels, "%s %u %016llX\n", named.name, edge, static_cast<unsigned long long>(hash)) < 0)
                return E_FAIL;
            ++frames;
        }
    }
    // A smaller glyph after the largest atlas must clear the whole output and ignore stale reusable coverage.
    if (FAILED(RedXeActions::RasterizeFluentGlyph(L'\xE713', 24, reference.get())) ||
        FAILED(batch.Rasterize(L'\xE713', 24, candidate.get())) ||
        std::memcmp(reference.get(), candidate.get(), 24 * 24 * sizeof(uint32_t)) != 0)
        return E_FAIL;
#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
    // Real CRT allocation failure after the font is ready: coverage growth keeps its preceding buffer usable.
    RedXeActions::FluentGlyphRasterizer failureBatch;
    if (FAILED(failureBatch.Rasterize(L'\xE713', 24, candidate.get())))
        return E_FAIL;
    HRESULT failed = S_OK;
    bool consumed = false;
    {
        const auto previous = _CrtSetAllocHook(RejectNextAllocation);
        const auto restore = wil::scope_exit(
            [&]() noexcept
            {
                gRejectNextAllocation = false;
                (void)_CrtSetAllocHook(previous);
            });
        gRejectNextAllocation = true;
        failed = failureBatch.Rasterize(L'\xE713', 512, candidate.get());
        consumed = !gRejectNextAllocation;
    }
    if (!consumed || failed != E_OUTOFMEMORY || FAILED(failureBatch.Rasterize(L'\xE713', 24, candidate.get())) ||
        std::memcmp(reference.get(), candidate.get(), 24 * 24 * sizeof(uint32_t)) != 0 ||
        FAILED(failureBatch.Rasterize(L'\xE713', 512, candidate.get())))
        return E_FAIL;
#endif
    std::wprintf(L"Shared Fluent rasterization: %u glyph/edge frames pass.\n", frames);
    return S_OK;
}
