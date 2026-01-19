cbuffer PaintParams : register(b0)
{
    float2 gScreenPx;
    float2 gViewportSize;

    float2 gTexSize;
    float gRootSize;
    float gRadiusPx;

    float gStrength;
    float3 _pad0;

    float4x4 gInvViewProj;
};

Texture2D<float4> gZW : register(t0);
RWTexture2D<float> gMask : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= (uint) gTexSize.x || tid.y >= (uint) gTexSize.y)
        return;

    int2 sp = int2(gScreenPx);
    float depth = gZW.Load(int3(sp, 0)).a;

    if (depth >= 0.99999f)
        return;

    float2 uv = gScreenPx / gViewportSize; 
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;

    float4 clip = float4(ndc.x, ndc.y, depth, 1.0f);

    float4 worldH = mul(clip, gInvViewProj);
    float3 world = worldH.xyz / worldH.w;

    float2 centerUV = float2(world.x / gRootSize + 0.5f,
                             world.z / gRootSize + 0.5f);

    if (centerUV.x < 0.0f || centerUV.x > 1.0f || centerUV.y < 0.0f || centerUV.y > 1.0f)
        return;

    float2 p = float2(tid.x + 0.5f, tid.y + 0.5f);
    float2 c = centerUV * gTexSize;

    float d = distance(p, c);
    if (d > gRadiusPx)
        return;

    float t = 1.0f - (d / gRadiusPx);
    float add = saturate(t) * gStrength;

    float old = gMask[tid.xy];
    gMask[tid.xy] = saturate(old + add);
}
