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

struct VertexOutput
{
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float4 color : COLOR0;
};

static const uint digitSegments[10] = {
    63,
    6,
    91,
    79,
    102,
    109,
    125,
    7,
    127,
    111,
};

static const float2 corners[4] = {
    float2(-1.0, -1.0),
    float2(-1.0, 1.0),
    float2(1.0, -1.0),
    float2(1.0, 1.0),
};

uint SelectTimeDigit(uint index)
{
    return index == 0 ? timeDigits.x : (index == 1 ? timeDigits.y : (index == 2 ? timeDigits.z : timeDigits.w));
}

uint SelectDateDigit(uint index)
{
    if (index < 4)
    {
        return index == 0 ? dateDigits0.x : (index == 1 ? dateDigits0.y : (index == 2 ? dateDigits0.z : dateDigits0.w));
    }
    index -= 4;
    return index == 0 ? dateDigits1.x : (index == 1 ? dateDigits1.y : (index == 2 ? dateDigits1.z : dateDigits1.w));
}

bool SegmentVisible(uint digit, uint segment)
{
    return (digitSegments[digit] & (1U << segment)) != 0;
}

float2 TimeSegmentPosition(float anchor, uint segment, uint dotIndex)
{
    static const float upperY[4] = {0.425, 0.4458, 0.4665, 0.4871};
    static const float lowerY[4] = {0.5139, 0.5348, 0.5554, 0.5762};
    if (segment == 0)
        return float2(anchor + float(dotIndex) * 0.02075, 0.4116);
    if (segment == 1)
        return float2(anchor + 0.0761 - float(dotIndex) * 0.00345, upperY[dotIndex]);
    if (segment == 2)
        return float2(anchor + 0.0614 - float(dotIndex) * 0.00345, lowerY[dotIndex]);
    if (segment == 3)
        return float2(anchor - 0.0295 + float(dotIndex) * 0.02075, 0.5897);
    if (segment == 4)
        return float2(anchor - 0.03325 - float(dotIndex) * 0.00345, lowerY[dotIndex]);
    if (segment == 5)
        return float2(anchor - 0.01835 - float(dotIndex) * 0.00345, upperY[dotIndex]);
    return float2(anchor - 0.01465 + float(dotIndex) * 0.02075, 0.50055);
}

float2 SecondsSegmentPosition(float anchor, uint segment, uint dotIndex)
{
    static const float upperY[3] = {0.6603, 0.6781, 0.6957};
    static const float lowerY[3] = {0.7199, 0.7378, 0.7554};
    if (segment == 0)
        return float2(anchor + float(dotIndex) * 0.0176, 0.6484);
    if (segment == 1)
        return float2(anchor + 0.0482 - float(dotIndex) * 0.0029, upperY[dotIndex]);
    if (segment == 2)
        return float2(anchor + 0.0382 - float(dotIndex) * 0.0029, lowerY[dotIndex]);
    if (segment == 3)
        return float2(anchor - 0.0199 + float(dotIndex) * 0.0176, 0.7677);
    if (segment == 4)
        return float2(anchor - 0.0271 - float(dotIndex) * 0.0029, lowerY[dotIndex]);
    if (segment == 5)
        return float2(anchor - 0.0171 - float(dotIndex) * 0.0029, upperY[dotIndex]);
    return float2(anchor - 0.0103 + float(dotIndex) * 0.0176, 0.7079);
}

float2 DateSegmentPosition(float anchor, uint segment, uint dotIndex)
{
    static const float upperY[3] = {1.033, 1.044, 1.055};
    static const float lowerY[3] = {1.069, 1.08, 1.091};
    if (segment == 0)
        return float2(anchor + float(dotIndex) * 0.0075, 1.025);
    if (segment == 1)
        return float2(anchor + 0.023 - float(dotIndex) * 0.0013, upperY[dotIndex]);
    if (segment == 2)
        return float2(anchor + 0.0185 - float(dotIndex) * 0.0013, lowerY[dotIndex]);
    if (segment == 3)
        return float2(anchor - 0.01 + float(dotIndex) * 0.0075, 1.099);
    if (segment == 4)
        return float2(anchor - 0.0115 - float(dotIndex) * 0.0013, lowerY[dotIndex]);
    if (segment == 5)
        return float2(anchor - 0.0075 - float(dotIndex) * 0.0013, upperY[dotIndex]);
    return float2(anchor - 0.0045 + float(dotIndex) * 0.0075, 1.062);
}

float2 TimeDot(uint instanceId, out float radius, out bool visible)
{
    radius = 0.008;
    if (instanceId >= 112)
    {
        visible = true;
        return instanceId == 112 ? float2(0.504, 0.4703) : float2(0.494, 0.5313);
    }

    const uint digitIndex = instanceId / 28;
    const uint slot = instanceId - digitIndex * 28;
    const uint segment = slot / 4;
    const uint dotIndex = slot - segment * 4;
    static const float anchors[4] = {0.215, 0.36265, 0.60145, 0.7488};
    visible = SegmentVisible(SelectTimeDigit(digitIndex), segment);
    return TimeSegmentPosition(anchors[digitIndex], segment, dotIndex);
}

float2 SecondsDot(uint instanceId, out float radius, out bool visible)
{
    radius = 0.008;
    const uint digitIndex = instanceId / 21;
    const uint slot = instanceId - digitIndex * 21;
    const uint segment = slot / 3;
    const uint dotIndex = slot - segment * 3;
    const uint digit = digitIndex == 0 ? secondsAndFlags.x : secondsAndFlags.y;
    const float anchor = digitIndex == 0 ? 0.4388 : 0.5436;
    visible = SegmentVisible(digit, segment);
    return SecondsSegmentPosition(anchor, segment, dotIndex);
}

float2 DateDot(uint instanceId, out float radius, out bool visible)
{
    radius = 0.006;
    if (instanceId >= 168)
    {
        const uint separatorCell = instanceId - 168;
        const uint separator = separatorCell / 3;
        const uint column = separatorCell - separator * 3;
        visible = true;
        return float2((separator == 0 ? 0.38125 : 0.51625) + (float(column) - 1.0) * 0.006, 1.062);
    }

    const uint digitIndex = instanceId / 21;
    const uint slot = instanceId - digitIndex * 21;
    const uint segment = slot / 3;
    const uint dotIndex = slot - segment * 3;
    static const float bases[8] = {0.286, 0.33, 0.421, 0.465, 0.556, 0.6, 0.644, 0.688};
    visible = SegmentVisible(SelectDateDigit(digitIndex), segment);
    return DateSegmentPosition(bases[digitIndex], segment, dotIndex);
}

float2 RingDot(uint instanceId, out float radius, out uint representedSecond)
{
    radius = 0.008;
    const bool emphasis = instanceId >= 60;
    representedSecond = emphasis ? (instanceId - 60) * 5 : instanceId;
    const float ringRadius = emphasis ? 0.447 : 0.418;
    const float angle = float(representedSecond) * 0.10471975511965977 - 1.5707963267948966;
    float sineValue = 0.0;
    float cosineValue = 0.0;
    sincos(angle, sineValue, cosineValue);
    return float2(0.5 + cosineValue * ringRadius, 0.5 + sineValue * ringRadius);
}

VertexOutput VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VertexOutput output;
    float radius = 0.0;
    float2 center = 0.0;
    float4 color = timeColor;
    bool visible = true;

    if (instanceId < segmentCounts.x)
    {
        center = TimeDot(instanceId, radius, visible);
    }
    else
    {
        instanceId -= segmentCounts.x;
        if (instanceId < segmentCounts.y)
        {
            center = SecondsDot(instanceId, radius, visible);
            color = secondsColor;
        }
        else
        {
            instanceId -= segmentCounts.y;
            if (instanceId < segmentCounts.z)
            {
                center = DateDot(instanceId, radius, visible);
            }
            else
            {
                instanceId -= segmentCounts.z;
                const bool externalDot = instanceId >= 60;
                uint representedSecond = 0;
                center = RingDot(instanceId, radius, representedSecond);
                color = secondsColor;
                const bool fullyLit = (externalDot && secondsAndFlags.w != 0) || representedSecond <= secondsAndFlags.z;
                color.a *= fullyLit ? 1.0 : 0.18;
            }
        }
    }

    const float2 corner = corners[vertexId];
    if (!visible)
    {
        output.position = float4(2.0, 2.0, 0.0, 1.0);
    }
    else
    {
        const float squareSize = geometry.x;
        const float2 pixel = viewportAndOrigin.zw + (center + corner * radius) * squareSize;
        const float2 viewport = viewportAndOrigin.xy;
        output.position = float4(pixel.x * 2.0 / viewport.x - 1.0, 1.0 - pixel.y * 2.0 / viewport.y, 0.0, 1.0);
    }
    output.local = corner;
    output.color = color;
    return output;
}
