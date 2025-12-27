#define PI 3.14159265f

Texture2D gInputImage : register(t0);
Texture2D gDepthMap : register(t1);
Texture2D gNormalMap : register(t2);

SamplerState gSampler : register(s0);

cbuffer PostProcessSettings : register(b0)
{
    float gFocusDistance;
    float gFocusRange;
    float gNearBlurStrength;
    float gFarBlurStrength;

    float2 gChromaticDirection;
    float gChromaticIntensity;
    float gChromaticDistanceScale;

    float gEffectIntensity;
    float gEffectType;
    float2 gPadding;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;

    float3 gSunDirW;
    float gSunIntensity;

    float3 gBetaRayleigh;
    float gRayleighScaleHeight;

    float3 gBetaMie;
    float gMieScaleHeight;

    float gMieG;
    float gAtmosphereDensity;
    float gExposure;
    float gEnableAtmosphere;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOut VS(uint vid : SV_VertexID)
{
    // fullscreen triangle
    float2 v[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };

    VSOut o;
    o.PosH = float4(v[vid], 0, 1);

    // NDC -> UV
    o.UV = o.PosH.xy * float2(0.5f, -0.5f) + 0.5f;
    return o;
}

float3 ReconstructWorldPos(float2 uv, float ndcDepth)
{
    float4 clip;
    clip.x = uv.x * 2.0f - 1.0f;
    clip.y = 1.0f - uv.y * 2.0f;
    clip.z = ndcDepth;
    clip.w = 1.0f;

    float4 w = mul(clip, gInvViewProj);
    w.xyz /= w.w;
    return w.xyz;
}

//phases
float PhaseRay(float mu)
{
    return 3.0f / (16.0f * PI) * (1.0f + mu * mu);
}

float PhaseMieHG(float mu, float g)
{
    float gg = g * g;
    float d = pow(max(1e-4f, 1.0f + gg - 2.0f * g * mu), 1.5f);
    return (1.0f - gg) / (4.0f * PI * d);
}


float Hash01(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

//exponential density by height
float DensityByHeight(float y, float baseY, float H, float densityMul)
{
    float h = max(0.0f, y - baseY);
    return exp(-h / max(1e-3f, H)) * densityMul;
}

struct FogMarchOut
{
    float3 AddLight; // accumulated in-scattering
    float3 Throughput; // transmittance
};

FogMarchOut MarchFog(float3 camPos, float3 viewDir, float maxDist, float baseY)
{
    FogMarchOut o;
    o.AddLight = 0.0f;
    o.Throughput = 1.0f;

    maxDist = max(maxDist, 1.0f);

    float t01 = saturate(maxDist / 80000.0f);
    int steps = (int) lerp(12.0f, 42.0f, t01);
    float dt = maxDist / steps;

    // jitter to reduce banding
    float j = (Hash01(viewDir.xz * 17.0f + camPos.xz * 0.001f + gTotalTime) - 0.5f) * dt;
    float t = 0.5f * dt + j;

    float3 sunDir = normalize(gSunDirW);
    float mu = dot(sunDir, viewDir);

    float pr = PhaseRay(mu);
    float pm = PhaseMieHG(mu, saturate(gMieG));

    pm = lerp(pm, 1.0f / (4.0f * PI), 0.35f);

    [loop]
    for (int i = 0; i < steps; ++i)
    {
        float3 p = camPos + viewDir * t;

        float rhoR = DensityByHeight(p.y, baseY, gRayleighScaleHeight, gAtmosphereDensity);
        float rhoM = DensityByHeight(p.y, baseY, gMieScaleHeight, gAtmosphereDensity);

        float3 sigmaExt = gBetaRayleigh * rhoR + (gBetaMie * 1.2f) * rhoM;

        float sunAirmass = 1.0f / max(0.2f, dot(float3(0, 1, 0), sunDir));
        float3 T_sun = exp(-sigmaExt * (12000.0f * sunAirmass));

        float3 S = (pr * gBetaRayleigh * rhoR + pm * gBetaMie * rhoM) * gSunIntensity * T_sun;

        o.AddLight += o.Throughput * S * dt;
        o.Throughput *= exp(-sigmaExt * dt);

        t += dt;
    }

    o.AddLight *= max(0.0f, gExposure);
    return o;
}

float4 PS(VSOut pin) : SV_Target
{
    uint2 pix = (uint2) pin.PosH.xy;

    float4 scene = gInputImage.Load(int3(pix, 0));
    if (gEnableAtmosphere < 0.5f)
        return scene;

    float ndcDepth = gDepthMap.Load(int3(pix, 0)).w;
    float3 nrm = gNormalMap.Load(int3(pix, 0)).rgb;
    float n2 = dot(nrm, nrm);

    bool isSky = (ndcDepth >= 1.0f - 1e-6f) || (n2 < 1e-6f);

    float3 camPos = gEyePosW;

    float3 P_far = ReconstructWorldPos(pin.UV, 0.0f);
    float3 viewDir = normalize(P_far - camPos);

    // These are “your style defaults”, not copied from the other shader
    const float GroundY = -40.0f; 
    const float FogTopY = 160.0f; 

    if (isSky)
    {
        float tMaxSky = 60000.0f;
        
        if (abs(viewDir.y) > 1e-6f)
        {
            float tTop = (FogTopY - camPos.y) / viewDir.y;
            if (tTop > 0.0f)
                tMaxSky = min(tMaxSky, tTop);
        }

        FogMarchOut fog = MarchFog(camPos, viewDir, tMaxSky, GroundY);
        float3 col = scene.rgb * fog.Throughput + fog.AddLight;
        return float4(col, 1.0f);
    }
    else
    {
        float3 wpos = ReconstructWorldPos(pin.UV, ndcDepth);
        float tMax = length(wpos - camPos);

        FogMarchOut fog = MarchFog(camPos, viewDir, tMax, GroundY);

        float3 col = scene.rgb * fog.Throughput + fog.AddLight;
        return float4(col, scene.a);
    }
}
