#pragma once

#include "../Common/d3dUtil.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"

struct PostProcessSettings {
    float FocusDistance;
    float FocusRange;
    float NearBlurStrength;
    float FarBlurStrength;

    DirectX::XMFLOAT2 ChromaticDirection;
    float ChromaticIntensity;
    float ChromaticDistanceScale;

    float EffectIntensity;
    int EffectType;
    float Padding[2];
};

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;
    //Atmosphere
    DirectX::XMFLOAT3 SunDirW = { 0.57735f, -0.57735f, 0.57735f }; // direction TO sun (world)
    float SunIntensity = 20.0f;

    DirectX::XMFLOAT3 BetaRayleigh = { 5.5e-6f, 13.0e-6f, 22.4e-6f }; // ~ RGB
    float RayleighScaleHeight = 8000.0f; // meters (or “units” if your world isn’t meters)

    DirectX::XMFLOAT3 BetaMie = { 21e-6f, 21e-6f, 21e-6f };
    float MieScaleHeight = 1200.0f;

    float MieG = 0.80f;         // anisotropy (0.7..0.9)
    float AtmosphereDensity = 1.0f; // overall multiplier (“dirty/clean”)
    float Exposure = 1.0f;      // post exposure for sky scattering
    float EnableAtmosphere = 1.0f; // 0/1

};



struct Vertex
{
    DirectX::XMFLOAT3 Tangent;
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexC;
};

// Stores the resources needed for the CPU to build the command lists
// for a frame.  
struct FrameResource
{
public:
    
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT lightCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    // We cannot reset the allocator until the GPU is done processing the commands.
    // So each frame needs their own allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    // We cannot update a cbuffer until the GPU is done processing the commands
    // that reference it.  So each frame needs their own cbuffers.
   // std::unique_ptr<UploadBuffer<FrameConstants>> FrameCB = nullptr;
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<PostProcessSettings>> PostProcessCB = nullptr;
    std::unique_ptr<UploadBuffer<LightConstants>> LightCB = nullptr;


    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 Fence = 0;
};