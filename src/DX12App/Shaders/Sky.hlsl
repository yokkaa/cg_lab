
SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

TextureCube gCubeMap : register(t0);

// Constant data that varies per object.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    uint gMaterialIndex;
    uint gObjPad0;
    uint gObjPad1;
    uint gObjPad2;
};

// Constant data that varies per frame.
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

    //Atmosphere
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

static const float PI = 3.14159265f;

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosL : POSITION; 
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    // Local vertex position as direction vector.
    vout.PosL = vin.PosL;

    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

    // Always center sky about camera.
    posW.xyz += gEyePosW;

    // Put skydome at far plane.
    vout.PosH = mul(posW, gViewProj).xyww;

    return vout;
}

//Phase functions
float PhaseRayleigh(float cosTheta)
{
    // 3/(16*pi) * (1 + cos^2)
    return (3.0f / (16.0f * PI)) * (1.0f + cosTheta * cosTheta);
}

float PhaseHG(float cosTheta, float g)
{
    // Henyey-Greenstein phase
    float g2 = g * g;
    float denom = pow(max(1e-3f, 1.0f + g2 - 2.0f * g * cosTheta), 1.5f);
    return (1.0f / (4.0f * PI)) * ((1.0f - g2) / denom);
}

// Cheap "air mass" approximation: more atmosphere near horizon
float AirMassApprox(float viewY)
{
    // viewY in [-1..1], where +1 is zenith, 0 is horizon
    float mu = saturate(viewY * 0.5f + 0.5f); // 0..1
    return lerp(8.0f, 1.0f, mu); // horizon ~8, zenith ~1
}

// Procedural sky color (single scattering-ish approximation)
float3 SkyColor(float3 viewDirW)
{
    viewDirW = normalize(viewDirW);
    float3 sunDir = normalize(gSunDirW);

    float cosTheta = dot(viewDirW, sunDir);

    // Strength multipliers ("clean/dirty")
    float3 betaR = gBetaRayleigh * gAtmosphereDensity;
    float3 betaM = gBetaMie * gAtmosphereDensity;

    float pr = PhaseRayleigh(cosTheta);
    float pm = PhaseHG(cosTheta, gMieG);

    // More optical depth near horizon
    float airMass = AirMassApprox(viewDirW.y);

    // Scale factor for your world units (tweakable but stable)
    // If sky is too dark -> lower this. If too bright/milky -> raise this a bit and lower exposure/density.
    float depthScale = 100000.0f;

    // Beer-Lambert transmittance through the atmosphere
    float3 tau = (betaR + betaM) * (airMass * depthScale);
    float3 T = exp(-tau);

    // In-scattering approx: proportional to (1 - T)
    float3 scatter = (betaR * pr + betaM * pm) * gSunIntensity * (1.0f - T);

    // Add tight sun disk / glow (helps УdirtyФ look)
    float sunDisk = pow(saturate(cosTheta), 5000.0f); // меньше диск
    float sunGlow = pow(saturate(cosTheta), 200.0f); // аккуратнее ореол
    scatter += (sunDisk * 0.8f + sunGlow * 0.08f) * gSunIntensity;


    // Simple exposure
    return scatter * gExposure;
}

float4 PS(VertexOut pin) : SV_Target
{
    // If atmosphere disabled -> old cubemap sky
    if (gEnableAtmosphere < 0.5f)
    {
        return gCubeMap.Sample(gsamLinearWrap, pin.PosL);
    }

    // Convert local direction to world direction using inverse view rotation.
    // This keeps sky aligned with camera orientation but not translation.
    float3 viewDirW = mul(pin.PosL, (float3x3) gInvView);

    float3 col = SkyColor(viewDirW);

    // Optional: clamp to avoid NaNs / crazy spikes
    col = max(col, 0.0f);
    // Reinhard tonemap (очень дешЄво и спасает от белого клипа)
    col = col / (1.0f + col);


    return float4(col, 1.0f);
}
