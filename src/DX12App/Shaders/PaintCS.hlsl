cbuffer PaintParams : register(b0)
{
    float gRadiusPx;
    float gStrength;
    float2 gTexSize;
    float _pad0;
};

StructuredBuffer<float2> gPickIn : register(t0); 
RWTexture2D<float> gMask : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= (uint) gTexSize.x || tid.y >= (uint) gTexSize.y)
        return;

    float2 centerUV = gPickIn[0];
    if (centerUV.x < 0.0f)
        return;

    float2 p = float2(tid.x + 0.5, tid.y + 0.5);
    float2 c = centerUV * gTexSize;

    float d = distance(p, c);
    if (d > gRadiusPx)
        return;

    float t = 1.0 - (d / gRadiusPx);
    float add = saturate(t) * gStrength;

    float old = gMask[tid.xy];
    gMask[tid.xy] = saturate(old + add);
}
