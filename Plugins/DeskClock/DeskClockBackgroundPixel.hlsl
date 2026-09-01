cbuffer DeskClockConstants : register(b0)
{
    float4 targetSize;
    float4 layout;
    float4 metrics;
    float4 backgroundColor;
    float4 cardColor;
    float4 digitColor;
    float4 dateColor;
    uint4 oldDigits0;
    uint4 oldDigits1;
    uint4 targetDigits0;
    uint4 targetDigits1;
    uint4 state;
    float4 animation;
    uint4 oldDate0;
    uint4 oldDate1;
    uint4 oldDate2;
    uint4 targetDate0;
    uint4 targetDate1;
    uint4 targetDate2;
    float4 oldDatePositions0;
    float4 oldDatePositions1;
    float4 oldDatePositions2;
    float4 targetDatePositions0;
    float4 targetDatePositions1;
    float4 targetDatePositions2;
};

float4 PixelMain() : SV_Target
{
    return backgroundColor;
}
