// Pixel entry point shared by every 5H4D3R5 shader; included after the shader's mainImage.
//
// SV_Position is in render-target pixels, so the viewport origin is removed and y is flipped to Shadertoy's
// bottom-left convention. An image pass clamps to the displayable range and applies the fade toward the dashboard
// background; a feedback-buffer pass (SHADERS_BUFFER_PASS) stores the raw value because the buffer is the
// shader's own state.

float4 PixelMain(float4 position : SV_Position) : SV_Target
{
    const float2 fragCoord = float2(position.x - iViewportOrigin.x, iResolution.y - (position.y - iViewportOrigin.y));
    float4 color = float4(0.0, 0.0, 0.0, 1.0);
    mainImage(color, fragCoord);
#if defined(SHADERS_BUFFER_PASS)
    return color;
#else
    // A NaN (GLSL leaves pow() of a negative base undefined, and a port inherits every such corner) must become a
    // dark pixel as on WebGL, not whatever the hardware makes of a NaN store: some GPUs write 1.0.
    float3 rgb = isnan(color.rgb) ? float3(0.0, 0.0, 0.0) : color.rgb;
    return float4(lerp(iBackground.rgb, saturate(rgb), iFade), 1.0);
#endif
}
