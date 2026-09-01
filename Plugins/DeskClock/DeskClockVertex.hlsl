static const float kPiOverTwo = 1.57079632679f;

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

cbuffer DeskClockDrawConstants : register(b1)
{
    uint4 drawState;
};

struct ClockOutput
{
    float4 position : SV_Position;
    float2 localUv : TEXCOORD0;
    nointerpolation uint kind : TEXCOORD1;
    nointerpolation uint glyph : TEXCOORD2;
    nointerpolation float shade : TEXCOORD3;
    nointerpolation float alpha : TEXCOORD4;
};

float2 Corner(uint vertexId)
{
    const float2 corners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
        float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
    };
    return corners[vertexId];
}

uint Digit(uint4 first, uint4 second, uint index)
{
    return index < 4U ? first[index] : second[index - 4U];
}

uint DateGlyph(uint4 first, uint4 second, uint4 third, uint index)
{
    return index < 4U ? first[index] : (index < 8U ? second[index - 4U] : third[index - 8U]);
}

float DatePosition(float4 first, float4 second, float4 third, uint index)
{
    return index < 4U ? first[index] : (index < 8U ? second[index - 4U] : third[index - 8U]);
}

float TileX(uint tile)
{
    const uint group = tile / 2U;
    const uint withinGroup = tile & 1U;
    return layout.x + float(tile) * layout.z + float(group) * metrics.y + float(withinGroup + group) * metrics.x;
}

float4 ToClip(float2 pixel)
{
    return float4(pixel.x * (2.0f / targetSize.x) - 1.0f,
                  1.0f - pixel.y * (2.0f / targetSize.y), 0.0f, 1.0f);
}

ClockOutput VertexMain(uint vertexId : SV_VertexID, uint rawInstanceId : SV_InstanceID)
{
    const uint instanceId = rawInstanceId + drawState.x;
    ClockOutput output;
    const float2 corner = Corner(vertexId);
    output.position = float4(-2.0f, -2.0f, 0.0f, 1.0f);
    output.localUv = corner;
    output.kind = 3U;
    output.glyph = 0U;
    output.shade = 1.0f;
    output.alpha = 0.0f;

    if (instanceId < 12U)
    {
        const uint tile = instanceId / 2U;
        const uint half = instanceId & 1U;
        const float localY = (float(half) + corner.y) * 0.5f;
        const bool changed = ((state.x >> tile) & 1U) != 0U && state.y != 0U;
        output.glyph = changed && half != 0U ? Digit(oldDigits0, oldDigits1, tile)
                                           : Digit(targetDigits0, targetDigits1, tile);
        output.localUv = float2(corner.x, localY);
        output.kind = 0U;
        output.alpha = 1.0f;
        output.position = ToClip(float2(TileX(tile) + corner.x * layout.z, layout.y + localY * layout.w));
        return output;
    }

    if (instanceId >= 16U && instanceId < 22U)
    {
        const uint tile = instanceId - 16U;
        if (((state.x >> tile) & 1U) == 0U || state.y == 0U)
        {
            return output;
        }

        const bool upper = state.y == 1U;
        const float localY = upper ? corner.y * 0.5f : 0.5f + corner.y * 0.5f;
        const float distanceFromHinge = abs(localY - 0.5f) * layout.w;
        const float angle = upper ? animation.x * kPiOverTwo : (1.0f - animation.x) * kPiOverTwo;
        const float projectedDistance = distanceFromHinge * cos(angle);
        const float pixelX = TileX(tile) + corner.x * layout.z;
        const float hingeY = layout.y + layout.w * 0.5f;
        const float pixelY = upper ? hingeY - projectedDistance : hingeY + projectedDistance;

        output.position = ToClip(float2(pixelX, pixelY));
        output.localUv = float2(corner.x, localY);
        output.kind = 1U;
        output.glyph = upper ? Digit(oldDigits0, oldDigits1, tile) : Digit(targetDigits0, targetDigits1, tile);
        output.shade = 0.56f + 0.44f * cos(angle);
        output.alpha = 1.0f;
        return output;
    }

    if (instanceId >= 32U && instanceId < 36U)
    {
        const uint dot = instanceId - 32U;
        const uint separator = dot / 2U;
        const uint afterTile = separator == 0U ? 1U : 3U;
        const float left = TileX(afterTile) + layout.z;
        const float right = TileX(afterTile + 1U);
        const float diameter = max(layout.z * 0.095f, 2.0f);
        const float centerX = (left + right) * 0.5f;
        const float centerY = layout.y + layout.w * (dot % 2U == 0U ? 0.38f : 0.62f);
        output.position = ToClip(float2(centerX - diameter * 0.5f + corner.x * diameter,
                                        centerY - diameter * 0.5f + corner.y * diameter));
        output.localUv = corner;
        output.kind = 2U;
        output.alpha = 1.0f;
        return output;
    }

    if (instanceId >= 36U && instanceId < 60U)
    {
        const bool target = instanceId >= 48U;
        const uint slot = target ? instanceId - 48U : instanceId - 36U;
        if (slot >= state.w)
        {
            return output;
        }
        const uint glyphCount = target ? targetDate2.w : oldDate2.w;
        if (slot >= glyphCount)
        {
            return output;
        }
        const uint glyph = target ? DateGlyph(targetDate0, targetDate1, targetDate2, slot)
                                  : DateGlyph(oldDate0, oldDate1, oldDate2, slot);
        if (glyph == 0xFFFFFFFFU)
        {
            return output;
        }

        const float glyphSize = layout.w * 0.20f;
        const float penPosition = target
                                      ? DatePosition(targetDatePositions0, targetDatePositions1,
                                                     targetDatePositions2, slot)
                                      : DatePosition(oldDatePositions0, oldDatePositions1, oldDatePositions2, slot);
        const float pixelX = targetSize.x * 0.5f + penPosition * glyphSize - glyphSize * (4.0f / 64.0f);
        output.position = ToClip(float2(pixelX + corner.x * glyphSize, metrics.z + corner.y * glyphSize));
        output.localUv = corner;
        output.kind = 3U;
        output.glyph = glyph;
        output.alpha = state.z == 0U ? (target ? 1.0f : 0.0f) : (target ? animation.y : 1.0f - animation.y);
        return output;
    }

    return output;
}
