cbuffer ClockConstants : register(b0)
{
    float4 backgroundColor;
    float4 timeColor;
    float4 secondsColor;
    float4 viewportAndOrigin;
    float4 geometry;
    uint4 timeDigits;
    uint4 secondsAndFlags;
    uint4 dateDigits0;
    uint4 dateDigits1;
    uint4 segmentCounts;
};

float4 PixelMain() : SV_Target
{
    return backgroundColor;
}
