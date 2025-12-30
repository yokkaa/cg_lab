cbuffer PickParams : register(b0)
{
    uint2 gMousePx; 
    float2 gScreenSize; 
    float4x4 gInvViewProj; 
    float gRootSize;
    float3 _pad;
};

Texture2D<float4> gZW : register(t0);
RWStructuredBuffer<float2> gPickOut : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint sx = gMousePx.x;
    uint sy = gMousePx.y;

    if (sx >= (uint) gScreenSize.x || sy >= (uint) gScreenSize.y)
    {
        gPickOut[0] = float2(-1, -1);
        return;
    }

    float depth01 = gZW.Load(int3((int) sx, (int) sy, 0)).w;

    if (depth01 >= 0.999999f)
    {
        gPickOut[0] = float2(-1, -1);
        return;
    }

    float2 ndc;
    ndc.x = ((sx + 0.5f) / gScreenSize.x) * 2.0f - 1.0f;
    ndc.y = 1.0f - ((sy + 0.5f) / gScreenSize.y) * 2.0f;

    float z = depth01 * 2.0f - 1.0f;

    float4 clip = float4(ndc.x, ndc.y, z, 1.0f);
    float4 world = mul(clip, gInvViewProj);
    world.xyz /= world.w;

    float2 uv = world.xz / gRootSize + 0.5f;

    gPickOut[0] = uv;
}
