#include "Common.hlsl"

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);
Texture2DArray gShadowMap : register(t3);
Texture2D gAOMap : register(t4);
Texture2D gPaintMask : register(t5);

struct VertexIn
{
    float3 Tangent : TANGENT;
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float3 Tangent : TANGENT;
    float4 PosW : POSITION;
    float4 PosH : SV_POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct PatchTess
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

struct GBufferData
{
    float4 diffuse : SV_TARGET0;
    float4 zwzanashih_RGBA32F : SV_TARGET1;
    float4 normal : SV_TARGET2;
    float4 materialAlbedo : SV_TARGET3;
    float4 MaterialFresnelRoughness : SV_TARGET4;
};

bool isVertexOnEdge(float2 TexC1, float2 TexC2)
{
    float2 f1 = frac(TexC1);
    float2 f2 = frac(TexC2);

    float eps = 1e-3f;

    bool left = (f1.x < eps && f2.x < eps);
    bool right = (f1.x > 1.0f - eps && f2.x > 1.0f - eps);
    bool bottom = (f1.y < eps && f2.y < eps);
    bool top = (f1.y > 1.0f - eps && f2.y > 1.0f - eps);

    return left || right || bottom || top;
}


VertexIn tessVS(VertexIn vin)
{
    vin.TexC = mul(float4(vin.TexC, 0.f, 1.f), gTexTransform).xy;
    return vin;
}

VertexOut VS(VertexIn vin)
{
    VertexOut vo;

    vo.Tangent = vin.Tangent;
    vo.PosW = mul(float4(vin.PosL, 1.0f), gWorld);
    vo.PosH = mul(vo.PosW, gViewProj);
    vo.NormalL = vin.NormalL;
    vo.TexC = mul(float4(vin.TexC, 0.f, 1.f), gTexTransform).xy;

    return vo;
}

VertexOut displaceVS(VertexIn vin)
{
    VertexOut vo;

    vo.Tangent = vin.Tangent;
    vo.PosW = mul(float4(vin.PosL, 1.0f), gWorld);
    vo.NormalL = vin.NormalL;
    vo.TexC = mul(float4(vin.TexC, 0.f, 1.f), gTexTransform).xy;

    // Displacement mapping
    float disp = gDisplacementMap.SampleLevel(gsamAnisotropicClamp, saturate(vo.TexC), 0).r;
    vo.PosW.y += disp * 250.0f;

    vo.PosH = mul(vo.PosW, gViewProj);

    return vo;
}

PatchTess ConstantHS(InputPatch<VertexIn, 3> patch, uint patchID : SV_PrimitiveID)
{
    PatchTess pt;

    float3 centerL = (patch[0].PosL + patch[1].PosL + patch[2].PosL) / 3.0f;
    float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;

    float dist = distance(centerW, gEyePosW);

    float nearDist = 150.0f;
    float farDist = 800.0f;

    float t = saturate((farDist - dist) / (farDist - nearDist)); // 0..1
    float tess = 1.0f + t * 15.0f; // 1..16

    pt.EdgeTess[0] = tess;
    pt.EdgeTess[1] = tess;
    pt.EdgeTess[2] = tess;
    pt.InsideTess = tess;

    return pt;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(64.0f)]
VertexIn HS(InputPatch<VertexIn, 3> p,
           uint i : SV_OutputControlPointID,
           uint patchId : SV_PrimitiveID)
{
    VertexIn hout;

    hout.PosL = p[i].PosL;
    hout.NormalL = p[i].NormalL;
    hout.TexC = p[i].TexC;
    hout.Tangent = p[i].Tangent;

    return hout;
}

[domain("tri")]
VertexOut DS(PatchTess patchTess,
             float3 bary : SV_DomainLocation,
             const OutputPatch<VertexIn, 3> tri)
{
    VertexOut dout;

    float3 p = bary.x * tri[0].PosL +
               bary.y * tri[1].PosL +
               bary.z * tri[2].PosL;

    float2 t = bary.x * tri[0].TexC +
               bary.y * tri[1].TexC +
               bary.z * tri[2].TexC;

    float eps = 0.5f / 1024.0f;
    t = clamp(t, eps, 1.0f - eps);

    float3 norm = bary.x * tri[0].NormalL +
                  bary.y * tri[1].NormalL +
                  bary.z * tri[2].NormalL;

    // Displacement mapping (stable)
    float heightScale = 250.0f;
    float disp = gDisplacementMap.SampleLevel(gsamAnisotropicClamp, t, 0).r;
    p.y += disp * heightScale;

    dout.PosW = mul(float4(p, 1.0f), gWorld);
    dout.PosH = mul(dout.PosW, gViewProj);
    dout.NormalL = norm;
    dout.Tangent = tri[0].Tangent;
    dout.TexC = t;

    return dout;
}

[maxvertexcount(21)]
void curtainsGS(triangle VertexOut p[3], inout TriangleStream<VertexOut> stream)
{
    stream.Append(p[0]);
    stream.Append(p[1]);
    stream.Append(p[2]);


    float downOffset = 200.f;

    int sides[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };

    VertexOut p1, p2;

    for (int i = 0; i < 3; i++)
    {
        p1 = p[sides[i][0]];
        p2 = p[sides[i][1]];

        if (isVertexOnEdge(p1.TexC, p2.TexC))
        {
            VertexOut p3 = p1;
            p3.PosW.y -= downOffset;
            p3.PosH = mul(p3.PosW, gViewProj);

            VertexOut p4 = p2;
            p4.PosW.y -= downOffset;
            p4.PosH = mul(p4.PosW, gViewProj);

            stream.Append(p1);
            stream.Append(p2);
            stream.Append(p4);

            stream.Append(p1);
            stream.Append(p3);
            stream.Append(p4);
        }
    }
}

GBufferData OriginalNormalPS(VertexOut pin)
{
    GBufferData pout;

    float2 uv = pin.TexC;
    float eps = 0.5f / 1024.0f;
    uv = clamp(uv, eps, 1.0f - eps);

    float3 normalMap = gNormalMap.Sample(gsamAnisotropicClamp, uv).rgb;
    float4 diffuseAlbedo = float4(0.5f, 0.5f, 0.5f, 1.0f);
    float ao = gAOMap.Sample(gsamAnisotropicClamp, uv).r;
    diffuseAlbedo.rgb *= ao;
    static const float gTerrainRootSize = 1024.0f; 

    float2 paintUV = float2(pin.PosW.x / gTerrainRootSize + 0.5f,
                        pin.PosW.z / gTerrainRootSize + 0.5f);

    paintUV = saturate(paintUV);

    float mask = gPaintMask.Sample(gsamLinearClamp, paintUV).r;
    diffuseAlbedo.rgb = lerp(diffuseAlbedo.rgb, float3(1, 0, 0), mask);

    pout.diffuse = diffuseAlbedo;
    pout.zwzanashih_RGBA32F = float4(0.f, 0.f, 0.f, pin.PosH.z);
    pout.normal = float4(normalMap, Metallic);
    pout.materialAlbedo = gDiffuseAlbedo;
    pout.MaterialFresnelRoughness = float4(gFresnelR0, gRoughness);

    return pout;
}

static const float gTerrainRootSize = 1024.0f; 

GBufferData DeferredPS(VertexOut pin)
{
    GBufferData pout;

    float2 uv = pin.TexC;
    float eps = 0.5f / 1024.0f;
    uv = clamp(uv, eps, 1.0f - eps);

    float3 normalMap = gNormalMap.Sample(gsamAnisotropicClamp, uv).rgb;
    normalMap = normalize(normalMap * 2.0f - 1.0f);

    if (length(normalMap) != 0.f)
    {
        float3 N = normalize(mul(pin.NormalL, (float3x3) gWorld));
        float3 T = normalize(mul(pin.Tangent, (float3x3) gWorld));
        float3 B = normalize(cross(N, T));

        float3x3 TBN = float3x3(T, B, N);
        
        normalMap = normalize(mul(normalMap, TBN));
    }
    else
    {
        normalMap = normalize(pin.NormalL);
    }

    float4 diffuseAlbedo = float4(0.5f, 0.5f, 0.5f, 1.0f);
    float ao = gAOMap.Sample(gsamAnisotropicClamp, uv).r;
    diffuseAlbedo.rgb *= ao;
    static const float gTerrainRootSize = 1024.0f;
    float2 paintUV = float2(pin.PosW.x / gTerrainRootSize + 0.5f,
                        pin.PosW.z / gTerrainRootSize + 0.5f);
    
    paintUV.y = 1.0f - paintUV.y;

    paintUV = saturate(paintUV);

    float mask = gPaintMask.Sample(gsamLinearClamp, paintUV).r;
    diffuseAlbedo.rgb = lerp(diffuseAlbedo.rgb, float3(1, 0, 0), mask);
    
    pout.diffuse = diffuseAlbedo;
    pout.zwzanashih_RGBA32F = float4(0.f, 0.f, 0.f, pin.PosH.z);
    pout.normal = float4(normalMap, Metallic);
    pout.materialAlbedo = gDiffuseAlbedo;
    pout.MaterialFresnelRoughness = float4(gFresnelR0, gRoughness);

    return pout;
}
