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

// Three depth layers: about a third of the streams in front at full brightness, the rest dimmer behind them.
float Depth(float roll)
{
    return roll < 0.34f ? 1.0f : (roll < 0.68f ? 0.6f : 0.34f);
}

// Brightness along a stream from its head (0) to its tail (1): flat for most of the length, a fade at the end.
float TrailProfile(float position)
{
    return smoothstep(0.0f, 0.45f, 1.0f - position) * lerp(0.82f, 1.0f, 1.0f - position);
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
    const float time = geometryAndTime.z;
    const float baseSpeed = geometryAndTime.w;
    const float baseTrailLength = float(stream.x);
    const float padding = float(stream.z);

    // Two streams per column, the way the film's rain overlaps: the main stream on every active column and a
    // shorter, sparser second one on roughly half of them, each with its own length, speed, and phase. The glyphs
    // stay put in the grid; a stream only lights them as its head passes. Each stream also sits at one of three
    // depths: a few bright, sharp streams in front over a field of dimmer ones, the film's layered look.
    const float trailA = baseTrailLength * lerp(0.62f, 1.3f, UnitFloat(Hash(streamHash ^ 0xB5297A4DU)));
    const float cycleA = float(rowCount) + trailA + padding;
    const float speedA = baseSpeed * lerp(0.55f, 1.5f, UnitFloat(Hash(streamHash ^ 0x68E31DA4U)));
    const float headA = fmod(time * speedA + UnitFloat(Hash(streamHash ^ 0x1B56C4E9U)) * cycleA, cycleA) - trailA;
    const float depthA = Depth(UnitFloat(Hash(streamHash ^ 0x9E3779B1U)));
    const uint secondHash = Hash(streamHash ^ 0x51ED270BU);
    const float hasSecond = (secondHash % 100U) < 45U ? 1.0f : 0.0f;
    const float trailB = baseTrailLength * lerp(0.45f, 0.9f, UnitFloat(Hash(secondHash ^ 0x2545F491U)));
    const float cycleB = float(rowCount) + trailB + padding * 2.2f;
    const float speedB = baseSpeed * lerp(0.5f, 1.6f, UnitFloat(Hash(secondHash ^ 0x7C3A9E11U)));
    const float headB = fmod(time * speedB + UnitFloat(Hash(secondHash ^ 0x3D4A8B2FU)) * cycleB, cycleB) - trailB;
    const float depthB = Depth(UnitFloat(Hash(secondHash ^ 0x6C8E9CF5U)));

    const float behindA = headA - float(row);
    const float visibleA = step(0.0f, behindA) * step(behindA, trailA);
    const float behindB = headB - float(row);
    const float visibleB = hasSecond * step(0.0f, behindB) * step(behindB, trailB);
    // The film's trail is lit evenly from the head down and only fades over its last stretch.
    const float trailShapeA = visibleA * depthA * TrailProfile(behindA / max(trailA, 1.0f));
    const float trailShapeB = visibleB * depthB * TrailProfile(behindB / max(trailB, 1.0f));
    const float headIntensity =
        max(visibleA * depthA * smoothstep(1.5f, 0.0f, behindA), visibleB * depthB * smoothstep(1.5f, 0.0f, behindB));

    // Each cell has its own brightness, a slow flicker, and a mutation schedule: it re-rolls its glyph every
    // 16 / mutationPerSecond seconds at its own phase and flashes as it does, so the field sparkles unevenly
    // instead of every glyph changing in step.
    const uint cellHash = Hash(targetAndSeed.z ^ (screenColumn * 0x85EBCA6BU) ^ (row * 0xC2B2AE35U));
    const float shade = lerp(0.76f, 1.0f, UnitFloat(cellHash));
    const float flicker = 1.0f + 0.12f * (UnitFloat(Hash(cellHash ^ uint(floor(time * 9.0f)))) - 0.5f);
    const float mutationClock = stream.y == 0U ? 0.0f : time * float(stream.y) * (1.0f / 16.0f) + UnitFloat(cellHash);
    const uint mutationTick = uint(floor(mutationClock));
    const float flash = stream.y == 0U ? 0.0f : pow(1.0f - frac(mutationClock), 8.0f) * 0.9f;
    const float trailIntensity = saturate(max(trailShapeA, trailShapeB) * shade * flicker * (1.0f + flash));

    const uint glyphIndex = Hash(cellHash ^ (mutationTick * 0x27D4EB2FU)) & 63U;
    const uint glyphX = glyphIndex & 7U;
    const uint glyphY = glyphIndex >> 3U;

    const float2 corner = corners[vertexId];
    const float glyphWidth = geometryAndTime.x * 0.855f;
    const float xMargin = (geometryAndTime.x - glyphWidth) * 0.5f;
    const float pixelX = effect.y + float(screenColumn) * geometryAndTime.x + xMargin + corner.x * glyphWidth;
    const float pixelY = float(row) * geometryAndTime.y + corner.y * geometryAndTime.y;

    GlyphOutput output;
    output.position = float4(pixelX * (2.0f / float(targetAndSeed.x)) - 1.0f,
                             1.0f - pixelY * (2.0f / float(targetAndSeed.y)), 0.0f, 1.0f);
    // 24 x 36 texel cells in a 192 x 288 atlas (GenerateGlyphAtlas.ps1); sample texel centres across the whole cell.
    const float2 atlasPixel = float2(float(glyphX * 24U), float(glyphY * 36U)) + 0.5f + corner * float2(23.0f, 35.0f);
    output.atlasUv = atlasPixel * float2(1.0f / 192.0f, 1.0f / 288.0f);
    output.trailIntensity = trailIntensity;
    output.headIntensity = headIntensity;
    return output;
}
