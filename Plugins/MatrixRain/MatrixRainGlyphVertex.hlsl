cbuffer MatrixRainConstants : register(b0)
{
    uint4 targetAndSeed;
    uint4 grid;
    uint4 stream;
    float4 geometryAndTime;
    float4 headColor;
    float4 trailColor;
    float4 backgroundColor;
    float4 effect;
};

struct GlyphOutput
{
    float4 position : SV_Position;
    float2 atlasUv : TEXCOORD0;
    float trailIntensity : TEXCOORD1;
    float headIntensity : TEXCOORD2;
};

uint Hash(uint value)
{
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

float UnitFloat(uint value)
{
    return float(value & 0x00FFFFFFU) * (1.0f / 16777216.0f);
}

GlyphOutput VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] = {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f),
    };

    const uint rowCount = targetAndSeed.w;
    const uint activeColumn = instanceId / rowCount;
    const uint row = instanceId - activeColumn * rowCount;
    const uint screenColumn = (activeColumn * grid.z + grid.w) % grid.x;
    const uint streamHash = Hash(targetAndSeed.z ^ (screenColumn * 0x9E3779B9U));

    const float baseTrailLength = float(stream.x);
    const float trailLength = baseTrailLength * lerp(0.72f, 1.28f, UnitFloat(Hash(streamHash ^ 0xB5297A4DU)));
    const float cycleLength = float(rowCount) + trailLength + float(stream.z);
    const float speed = geometryAndTime.w * lerp(0.68f, 1.42f, UnitFloat(Hash(streamHash ^ 0x68E31DA4U)));
    const float phase = UnitFloat(Hash(streamHash ^ 0x1B56C4E9U)) * cycleLength;
    const float headRow = fmod(geometryAndTime.z * speed + phase, cycleLength) - trailLength;
    const float distanceBehindHead = headRow - float(row);
    const float visible = step(0.0f, distanceBehindHead) * step(distanceBehindHead, trailLength);
    const float normalizedTrail = saturate(1.0f - distanceBehindHead / max(trailLength, 1.0f));
    const float trailIntensity = visible * normalizedTrail * normalizedTrail;
    const float headIntensity = visible * smoothstep(1.35f, 0.0f, distanceBehindHead);

    const uint mutationTick = stream.y == 0U ? 0U : uint(floor(geometryAndTime.z * float(stream.y)));
    const uint glyphIndex = Hash(targetAndSeed.z ^ (screenColumn * 0x85EBCA6BU) ^ (row * 0xC2B2AE35U) ^ mutationTick) &
                            63U;
    const uint glyphX = glyphIndex & 7U;
    const uint glyphY = glyphIndex >> 3U;

    const float2 corner = corners[vertexId];
    const float glyphWidth = geometryAndTime.x * 0.84f;
    const float xMargin = (geometryAndTime.x - glyphWidth) * 0.5f;
    const float pixelX = effect.y + float(screenColumn) * geometryAndTime.x + xMargin + corner.x * glyphWidth;
    const float pixelY = float(row) * geometryAndTime.y + corner.y * geometryAndTime.y;

    GlyphOutput output;
    output.position = float4(pixelX * (2.0f / float(targetAndSeed.x)) - 1.0f,
                             1.0f - pixelY * (2.0f / float(targetAndSeed.y)), 0.0f, 1.0f);
    const float2 atlasPixel = float2(float(glyphX * 16U), float(glyphY * 16U)) + 0.5f + corner * 15.0f;
    output.atlasUv = atlasPixel * (1.0f / 128.0f);
    output.trailIntensity = trailIntensity;
    output.headIntensity = headIntensity;
    return output;
}
