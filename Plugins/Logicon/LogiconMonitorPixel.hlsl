Texture2D faceTexture : register(t0);
SamplerState faceSampler : register(s0);

struct MonitorInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 PixelMain(MonitorInput input) : SV_Target
{
    const float4 sample = faceTexture.Sample(faceSampler, input.uv);
    return float4(sample.rgb, 1.0f);
}
