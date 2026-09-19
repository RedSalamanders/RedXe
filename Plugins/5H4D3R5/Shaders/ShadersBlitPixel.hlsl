// Upscales the image pass from the offscreen texture into the widget viewport when renderScalePercent < 100.
// The image pass filled only the top-left iBlitScale fraction of the texture, so the sample rectangle stops half a
// texel inside that region and never blends with stale texels from a larger earlier draw.
#include "ShadersCommon.hlsli"

float4 PixelMain(float4 position : SV_Position) : SV_Target
{
    float2 uv = (position.xy - iViewportOrigin) / iResolution.xy * iBlitScale;
    float2 textureSize;
    iChannel0.GetDimensions(textureSize.x, textureSize.y);
    const float2 halfTexel = 0.5 / textureSize;
    uv = clamp(uv, halfTexel, iBlitScale - halfTexel);
    return float4(iChannel0.Sample(iChannel0Sampler, uv).rgb, 1.0);
}
