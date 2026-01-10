#define NOMINMAX

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/Camera.h"
#include "FrameResource.h"
#include "ShadowMap.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

//#define DEBUG_VIEW
// #define DEBUG

const int gNumFrameResources = 3;

enum class RenderLayer : int
{
	Opaque = 0,
	Debug,
	Sky,
	Terrain,
	Count
};

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
	BoundingBox Bounds;
	std::string geoName;
	int layer;
	std::vector<std::string> LODGeoNames;
	int currentLOD = 0;
};

struct Node
{
	RenderItem* RItem = nullptr;
	Node* children[4] = { nullptr };
	int layer = 0;
	bool hasChildren = false;
};

struct LightObject
{
	DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
	float FalloffStart = 1.0f;                          // point/spot light only
	DirectX::XMFLOAT3 Direction = { 0.57735f, -0.57735f, 0.57735f };// directional/spot light only
	float FalloffEnd = 50.0f;                           // point/spot light only
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
	float SpotPower = 64.0f;                            // spot light only
	DirectX::XMFLOAT3 Color = { 1.f, 1.f, 1.f };        // rgb
	LightType LightType = LightType::Directional;
	int lightCBIndex = 0;
	int NumFramesDirty = gNumFrameResources;
	std::string GeoName;
	ShadowMap* shadowMap;
};

struct PaintParamsCB
{
	DirectX::XMFLOAT2 CenterUV = { -1.0f, -1.0f }; 
	float RadiusPx = 20.0f;                     
	float Strength = 0.35f;                      

	DirectX::XMFLOAT2 TexSize = { 1024.0f, 1024.0f }; 
	float pad0 = 0.0f;
	float pad1 = 0.0f;
};


class DX12App : public D3DApp
{
public:
	DX12App(HINSTANCE hInstance);
	DX12App(const DX12App& rhs) = delete;
	DX12App& operator=(const DX12App& rhs) = delete;
	~DX12App();

	virtual bool Initialize()override;

private:
	virtual void CreateRtvAndDsvDescriptorHeaps()override;
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;
	virtual void OnMouseWheel(WPARAM btnState)override;

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateLightCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdatePostProcessCB(const GameTimer& gt);

	void LoadTexture(std::string name, std::wstring filename, TextureType type = TextureType::TEXTURE2D);
	void LoadTextures();
	void LoadTerrainTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	RenderItem* BuildRenderItem(std::string name, std::string material, XMMATRIX translate, std::vector<std::string>* LODGeoNames, int layer = (int)RenderLayer::Opaque, float scale = 1.f, float scaleTex = 1.f);
	void BuildRenderItems();
	void BuildLightObjects();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawDeferredGeometry();
	void DrawDeferredLights();
	void DrawSkyBox();
	void DrawPostProcess();
	void BuildPaintMask();
	void BuildPaintCompute();
	void ExecutePaintStrokes();
	bool PickTerrainUV(int sx, int sy, float& outU, float& outV);

	void DrawShadowMaps();
	void UpdateSkyBoxRotation();

	static bool RayTriangleIntersect(
		const DirectX::XMVECTOR& rayOrigin,
		const DirectX::XMVECTOR& rayDir,
		const DirectX::XMVECTOR& v0,
		const DirectX::XMVECTOR& v1,
		const DirectX::XMVECTOR& v2,
		float& t, float& u, float& v);


	// Quad Tree for Terrain
	Node* BuildNode(int layer, float x, float y, int xi, int yi);
	void BuildTerrainQuadTree();
	void UpdateVisibleTerrainTiles();
	void ChooseVisibleTerrainTile(Node* node);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	std::unordered_map<std::string, ComPtr<ID3D12RootSignature>> mRootSignature;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	int ObjCBIndex = 0;
	std::vector<std::unique_ptr<LightObject>> mAllLights;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];
	std::vector<RenderItem*> mVisibleRitems[(int)RenderLayer::Count];
	std::vector<RenderItem*> mVisibleTerrain;

	PassConstants mMainPassCB;

	Camera mCamera;
	POINT mLastMousePos;
	RenderItem* mSkyRitem = nullptr; 
	DirectX::XMFLOAT3 mSkySunRefDir = { 0.0f, 0.0f, 1.0f };
	float mSkyRoll = 0.0f;

	UINT mShadowMapHeapIndex = 0;

	// Quad tree
	Node* root = nullptr;
	int layers = 4;
	float RootSize = 1024.f;
	float thresholds[5] = {1500.f, 1000.f, 500.f, 200.f, 100.f};




	// Atmosphere controls
	float mAtmoDensity = 1.0f;
	float mMieG = 0.80f;
	float mMieStrength = 1.0f;      // multiplies BetaMie
	float mRayleighStrength = 1.0f; // multiplies BetaRayleigh
	float mAtmoExposure = 1.0f;
	bool  mAtmoEnabled = true;

	// --- Sun animation (for sunrise/sunset) ---
	float mSunAngle = 0.35f;  
	float mSunAzimuth = 0.25f; 
	bool  mSunAuto = false;    
	float mSunSpeed = 0.15f;     

	static const UINT PaintW = 1024;
	static const UINT PaintH = 1024;

	ComPtr<ID3D12Resource> mPaintMask = nullptr;

	UINT mPaintMaskSrvIndex = 0;
	UINT mPaintMaskUavIndex = 0;

	ComPtr<ID3D12RootSignature> mPaintRootSig = nullptr;
	ComPtr<ID3D12PipelineState> mPaintPSO = nullptr;

	struct PaintStroke
	{
		int sx = 0, sy = 0;
		float radius = 30.0f;
		float strength = 0.35f;
	};

	std::vector<PaintStroke> mPendingStrokes;

	std::unique_ptr<UploadBuffer<PaintStroke>> mPaintCB = nullptr;
	std::unique_ptr<UploadBuffer<PaintParamsCB>> mPaintParamsCB;
	PaintParamsCB mPaintParamsData;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	PSTR lpCmdLine, int nCmdShow)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		DX12App theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

DX12App::DX12App(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

DX12App::~DX12App()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool DX12App::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mCamera.SetPosition(0.0f, 300.0f, -500.0f);
	mCamera.Pitch(-5.3f);
	mCamera.RotateY(0.7f);

	LoadTextures();
	LoadTerrainTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	mPaintMaskSrvIndex = mShadowMapHeapIndex + 32;
	mPaintMaskUavIndex = mPaintMaskSrvIndex + 1;
	BuildPaintMask();

	BuildShadersAndInputLayout();

	BuildPaintCompute();

	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildTerrainQuadTree();
	BuildLightObjects();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();
	mPaintParamsCB = std::make_unique<UploadBuffer<PaintParamsCB>>(md3dDevice.Get(), 1, true);


	return true;
}

void DX12App::CreateRtvAndDsvDescriptorHeaps()
{
	// Add +6 RTV for cube render target.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	// Add +199 DSV for shadow map.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 200;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void DX12App::OnResize()
{ 
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 100000.0f);
	mGBuffer->Resize(mClientWidth, mClientHeight, md3dDevice.Get());

	// copy gbuffer resources into the srv heap
	if (mSrvDescriptorHeap != nullptr)
	{
		auto srvGBuffer = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		srvGBuffer.Offset(mGBuffer->Channel0SRVHeapIndex, mCbvSrvDescriptorSize);
		md3dDevice->CopyDescriptorsSimple(mGBuffer->NumBuffers, srvGBuffer,
			mGBuffer->m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
}

void DX12App::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);

	UpdateMainPassCB(gt);     
	UpdateSkyBoxRotation();    

	UpdateObjectCBs(gt);       
	UpdateVisibleTerrainTiles();
	UpdateLightCBs(gt);
	UpdateMaterialCBs(gt);
	UpdatePostProcessCB(gt);

	wchar_t caption[256];
	swprintf_s(caption, L"Atmosphere: dens=%.2f mie=%.2f ray=%.2f g=%.2f %s  (C=Clean, V=Dirty, T=Toggle)",
		mAtmoDensity, mMieStrength, mRayleighStrength, mMieG,
		mAtmoEnabled ? L"ON" : L"OFF");
	SetWindowText(mhMainWnd, caption);

}

void DX12App::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	auto passCB = mCurrFrameResource->PassCB->Resource();
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));
	mCommandList->SetGraphicsRootSignature(mRootSignature["default"].Get());

	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	
	DrawShadowMaps();

	mGBuffer->TransitToOpaqueRenderingState(mCommandList);
	mGBuffer->ClearRTVs(mCommandList);

	DrawDeferredGeometry();
	ExecutePaintStrokes();

	mGBuffer->TransitToLightsRenderingState(mCommandList);
	DrawDeferredLights();
	DrawSkyBox();

	mGBuffer->TransitToTonemappingState(mCommandList);
	DrawPostProcess();

	mGBuffer->TransitFromShaderResourceToCommon(mCommandList);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void DX12App::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);

	if (btnState & MK_LBUTTON)
	{
		PaintStroke s;
		s.sx = x;
		s.sy = y;
		s.radius = 30.0f;
		s.strength = 0.35f;
		mPendingStrokes.push_back(s);
	}


}

void DX12App::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void DX12App::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void DX12App::OnMouseWheel(WPARAM btnState)
{
	short wheelDelta = GET_WHEEL_DELTA_WPARAM(btnState);

	float& speed = mCamera.speed;
	if (wheelDelta > 0)
		speed = std::min(speed + 4.0f, 5000.0f);
	else if (wheelDelta < 0)
		speed = (speed - 4.0f) > 1.0f ? (speed - 1.0f) : 1.0f;

}

void DX12App::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(mCamera.speed * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-mCamera.speed * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-mCamera.speed * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(mCamera.speed * dt);

	mCamera.UpdateViewMatrix();

	if (mSunAuto)
	{
		mSunAngle += mSunSpeed * dt;
		if (mSunAngle > XM_PIDIV2 - 0.05f) { mSunAngle = XM_PIDIV2 - 0.05f; mSunSpeed = -fabsf(mSunSpeed); }
		if (mSunAngle < -0.15f) { mSunAngle = -0.15f;            mSunSpeed = fabsf(mSunSpeed); }
	}

	// DEBUG: camera look + visible terrain
	XMFLOAT3 look = mCamera.GetLook3f();

	char buf[256];
	sprintf_s(
		buf,
		"Look = (%.2f %.2f %.2f) | VisibleTerrain = %d\n",
		look.x, look.y, look.z,
		(int)mVisibleTerrain.size()
	);

	OutputDebugStringA(buf);
	auto clamp = [](float v, float a, float b) { return std::max(a, std::min(v, b)); };

	if (GetAsyncKeyState('T') & 0x0001) mAtmoEnabled = !mAtmoEnabled;

	// Clean preset
	if (GetAsyncKeyState('C') & 0x8000)
	{
		mAtmoDensity = 0.6f;
		mMieStrength = 0.4f;
		mRayleighStrength = 1.2f;
		mMieG = 0.75f;
		mAtmoExposure = 1.1f;
	}
	// Dirty preset
	if (GetAsyncKeyState('V') & 0x8000)
	{
		mAtmoDensity = 2.0f;
		mMieStrength = 2.2f;
		mRayleighStrength = 0.7f;
		mMieG = 0.88f;
		mAtmoExposure = 1.0f;
	}

	// Fine controls
	if (GetAsyncKeyState('1') & 0x8000) mAtmoDensity = clamp(mAtmoDensity - 0.02f, 0.0f, 5.0f);
	if (GetAsyncKeyState('2') & 0x8000) mAtmoDensity = clamp(mAtmoDensity + 0.02f, 0.0f, 5.0f);

	if (GetAsyncKeyState('3') & 0x8000) mMieStrength = clamp(mMieStrength - 0.02f, 0.0f, 5.0f);
	if (GetAsyncKeyState('4') & 0x8000) mMieStrength = clamp(mMieStrength + 0.02f, 0.0f, 5.0f);

	if (GetAsyncKeyState('5') & 0x8000) mRayleighStrength = clamp(mRayleighStrength - 0.02f, 0.0f, 5.0f);
	if (GetAsyncKeyState('6') & 0x8000) mRayleighStrength = clamp(mRayleighStrength + 0.02f, 0.0f, 5.0f);

	if (GetAsyncKeyState('7') & 0x8000) mMieG = clamp(mMieG - 0.002f, 0.60f, 0.95f);
	if (GetAsyncKeyState('8') & 0x8000) mMieG = clamp(mMieG + 0.002f, 0.60f, 0.95f);

	// --- Sun controls ---

	if (GetAsyncKeyState('R') & 0x0001) mSunAuto = !mSunAuto;

	if (GetAsyncKeyState('I') & 0x8000) mSunAngle += 0.6f * dt; // вверх
	if (GetAsyncKeyState('K') & 0x8000) mSunAngle -= 0.6f * dt; // вниз

	if (GetAsyncKeyState('J') & 0x8000) mSunAzimuth -= 0.8f * dt;
	if (GetAsyncKeyState('L') & 0x8000) mSunAzimuth += 0.8f * dt;

	mSunAngle = clamp(mSunAngle, -0.15f, XM_PIDIV2 - 0.05f);



}

void DX12App::AnimateMaterials(const GameTimer& gt)
{

}

void DX12App::UpdateObjectCBs(const GameTimer& gt)
{
	for (int i = 0; i < (int)RenderLayer::Count; i++)
		mVisibleRitems[i].clear();

	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		XMMATRIX world = XMLoadFloat4x4(&e->World);
		
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);
			const std::string& drawName =
				(!e->LODGeoNames.empty() && e->currentLOD < (int)e->LODGeoNames.size())
				? e->LODGeoNames[e->currentLOD]
				: e->geoName;
			e->Geo->DrawArgs[e->geoName].Bounds.Transform(e->Bounds, XMLoadFloat4x4(&e->World));
			if (e->layer == (int)RenderLayer::Terrain)
			{
				const float maxHeight = 260.0f;
				e->Bounds.Extents.y += maxHeight * 0.5f;
				e->Bounds.Center.y += maxHeight * 0.5f;
			}



			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}

		if (mCamera.Bounds.Intersects(e->Bounds))
		{
			mVisibleRitems[e->layer].push_back(e.get());

			XMVECTOR worldPos, temp;
			XMMatrixDecompose(&temp, &temp, &worldPos, world);
			float camToObjDistance;
			XMStoreFloat(&camToObjDistance, XMVector3Length(XMVectorSubtract(mCamera.GetPosition(), worldPos)));

			if (camToObjDistance > 150.f)
				e->currentLOD = 1;
			else e->currentLOD = 0;
		}
	}
}

void DX12App::UpdateLightCBs(const GameTimer& gt)
{
	auto currLightCB = mCurrFrameResource->LightCB.get();
	for (auto& e : mAllLights)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			LightConstants lightConstants;
			lightConstants.Strength = e->Strength;
			lightConstants.FalloffStart = e->FalloffStart;
			lightConstants.Direction = e->Direction;
			lightConstants.FalloffEnd = e->FalloffEnd;
			lightConstants.Position = e->Position;
			lightConstants.SpotPower = e->SpotPower;
			lightConstants.Color = e->Color;
			lightConstants.LightType = (int)e->LightType;

			XMVECTOR lightDir, lightPos, targetPos;
			XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			XMMATRIX lightView, lightProj;
			// to transform NDC space [-1,+1]^2 to texture space [0,1]^2
			XMMATRIX T(
				0.5f, 0.0f, 0.0f, 0.0f,
				0.0f, -0.5f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.5f, 0.5f, 0.0f, 1.0f);

			switch (e->LightType)
			{
			case LightType::Directional:
			{
				float SphereRadiuses[4] = { 100, 150, 200, 500 };

				//for each cascade
				for (int i = 0; i < 4; i++)
				{
					// Only the first "main" light casts a shadow. Why? Idk, you tell me.
					lightDir = XMLoadFloat3(&lightConstants.Direction);
					lightPos = mCamera.GetPosition() - 2.0f * SphereRadiuses[i] * lightDir;
					targetPos = mCamera.GetPosition();
					lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

					// Transform bounding sphere to light space.
					XMFLOAT3 sphereCenterLS;
					XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

					// Ortho frustum in light space encloses scene.
					float l = sphereCenterLS.x - SphereRadiuses[i];
					float b = sphereCenterLS.y - SphereRadiuses[i];
					float n = sphereCenterLS.z - SphereRadiuses[i];
					float r = sphereCenterLS.x + SphereRadiuses[i];
					float t = sphereCenterLS.y + SphereRadiuses[i];
					float f = sphereCenterLS.z + SphereRadiuses[i];

					lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

					XMMATRIX S = lightView * lightProj;
					XMMATRIX S1 = S * T;
					XMStoreFloat4x4(&lightConstants.ViewProj[i], XMMatrixTranspose(S));
					XMStoreFloat4x4(&lightConstants.ShadowTransform[i], XMMatrixTranspose(S1));
				}
				break;
			}
			case LightType::Spotlight:
			{
				XMFLOAT3 ConeScale;
				ConeScale.y = e->FalloffEnd;
				ConeScale.x = 20 / ConeScale.y;
				ConeScale.x = ConeScale.z = ConeScale.x * e->SpotPower * 8;
				//calculate rotation matrix from start and target direction vectors
				XMVECTOR StartDir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
				XMVECTOR TargetDir = XMVector3Normalize(XMLoadFloat3(&e->Direction));
				XMVECTOR RotationAxis = XMVector3Cross(StartDir, TargetDir);
				float RotAngle = acosf(XMVectorGetX(XMVector3Dot(StartDir, TargetDir)));

				XMStoreFloat4x4(&lightConstants.World, XMMatrixTranspose(
					XMMatrixScaling(ConeScale.x, ConeScale.y, ConeScale.z) *
					XMMatrixRotationAxis(XMVector3Normalize(RotationAxis), RotAngle) *
					XMMatrixTranslation(e->Position.x, e->Position.y, e->Position.z)));

				lightPos = XMLoadFloat3(&e->Position);
				lightDir = XMLoadFloat3(&e->Direction);
				lightPos = lightPos - 20 * lightDir;
				targetPos = lightPos + lightDir;
				lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
				lightProj = XMMatrixPerspectiveFovLH(XM_PI / 2.5f, 1.0f, 10.f, e->FalloffEnd * 10);

				XMMATRIX S = lightView * lightProj;
				XMMATRIX S1 = S * T;

				XMStoreFloat4x4(&lightConstants.ViewProj[0], XMMatrixTranspose(S));
				XMStoreFloat4x4(&lightConstants.ShadowTransform[0], XMMatrixTranspose(S1));
				break;
			}
			case LightType::Pointlight:
			{
				XMStoreFloat4x4(&lightConstants.World,
					XMMatrixTranspose(XMMatrixScaling(e->FalloffEnd * e->Strength.x, e->FalloffEnd * e->Strength.y, e->FalloffEnd * e->Strength.z)
					* XMMatrixTranslation(e->Position.x, e->Position.y, e->Position.z)));

				lightPos = XMLoadFloat3(&e->Position);

				lightProj = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, 0.1f, e->FalloffEnd);

				static const XMVECTOR directions[6] =
				{
				 XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),  // +X
				 XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f), // -X
				 XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),  // +Y
				 XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f), // -Y
				 XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),  // +Z
				 XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f)  // -Z
				};

				static const XMVECTOR ups[6] =
				{
				 XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),  // +X
				 XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),  // -X
				 XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), // +Y
				 XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),  // -Y
				 XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),  // +Z
				 XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)   // -Z
				};

				for (int i = 0; i < 6; ++i)
				{
					targetPos = lightPos + directions[i];
					lightView = XMMatrixLookAtLH(lightPos, targetPos, ups[i]);

					XMMATRIX S = lightView * lightProj;
					XMMATRIX S1 = S * T;
					XMStoreFloat4x4(&lightConstants.ViewProj[i], XMMatrixTranspose(S));
					XMStoreFloat4x4(&lightConstants.ShadowTransform[i], XMMatrixTranspose(S1));
				}
				break;
			}
			}

			currLightCB->CopyData(e->lightCBIndex, lightConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void DX12App::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));
			matConstants.Metallic = mat->Metallic;

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void DX12App::UpdatePostProcessCB(const GameTimer& gt)
{
	auto currPostProcessCB = mCurrFrameResource->PostProcessCB.get();
	PostProcessSettings postProcessSettings;

	postProcessSettings.FocusDistance = 0.95f;
	postProcessSettings.FocusRange = 0.1f;
	postProcessSettings.NearBlurStrength = 5.0f;
	postProcessSettings.FarBlurStrength = 5.0f;
	postProcessSettings.ChromaticDirection = XMFLOAT2(-1.0f, -1.0f);
	postProcessSettings.ChromaticIntensity = 2.0f;
	postProcessSettings.ChromaticDistanceScale = 1.5f;
	postProcessSettings.EffectIntensity = 0.0f;
	postProcessSettings.EffectType = 0;

	currPostProcessCB->CopyData(0, postProcessSettings);
}

void DX12App::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 100000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	DirectX::XMFLOAT3 sunDir;
	sunDir.y = sinf(mSunAngle);                
	float h = cosf(mSunAngle);                   
	sunDir.x = h * cosf(mSunAzimuth);
	sunDir.z = h * sinf(mSunAzimuth);

	XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&sunDir));
	XMStoreFloat3(&sunDir, v);

	mMainPassCB.SunDirW = sunDir;
	mMainPassCB.SunIntensity = 5.0f;


	mMainPassCB.BetaRayleigh = DirectX::XMFLOAT3(5.5e-6f, 13.0e-6f, 22.4e-6f);
	mMainPassCB.BetaMie = DirectX::XMFLOAT3(21e-6f, 21e-6f, 21e-6f);

	mMainPassCB.AtmosphereDensity = mAtmoDensity;
	mMainPassCB.MieG = mMieG;
	mMainPassCB.Exposure = mAtmoExposure;
	mMainPassCB.EnableAtmosphere = mAtmoEnabled ? 1.0f : 0.0f;

	mMainPassCB.BetaRayleigh.x *= mRayleighStrength;
	mMainPassCB.BetaRayleigh.y *= mRayleighStrength;
	mMainPassCB.BetaRayleigh.z *= mRayleighStrength;

	mMainPassCB.BetaMie.x *= mMieStrength;
	mMainPassCB.BetaMie.y *= mMieStrength;
	mMainPassCB.BetaMie.z *= mMieStrength;

	mMainPassCB.RayleighScaleHeight = 8000.0f;
	mMainPassCB.MieScaleHeight = 1200.0f;

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

static float Clamp01(float v) { return (v < 0.f) ? 0.f : (v > 1.f ? 1.f : v); }

void DX12App::UpdateSkyBoxRotation()
{
	if (!mSkyRitem) return;

	using namespace DirectX;

	XMVECTOR target = XMVector3Normalize(XMLoadFloat3(&mMainPassCB.SunDirW));

	XMVECTOR ref = XMVector3Normalize(XMLoadFloat3(&mSkySunRefDir));

	XMVECTOR axis = XMVector3Cross(ref, target);


	float d;
	XMStoreFloat(&d, XMVector3Dot(ref, target));
	d = std::max(-1.0f, std::min(1.0f, d));
	float angle = acosf(d);

	XMVECTOR axisLen2 = XMVector3Dot(axis, axis);
	float a2; XMStoreFloat(&a2, axisLen2);

	XMMATRIX Ralign = XMMatrixIdentity();

	if (a2 > 1e-8f)
	{
		axis = XMVector3Normalize(axis);
		Ralign = XMMatrixRotationAxis(axis, angle);
	}
	else
	{

		if (d < -0.999f)
		{
			XMVECTOR any = XMVectorSet(0, 1, 0, 0);
			XMVECTOR altAxis = XMVector3Cross(ref, any);
			float alt2; XMStoreFloat(&alt2, XMVector3Dot(altAxis, altAxis));
			if (alt2 < 1e-8f)
				altAxis = XMVector3Cross(ref, XMVectorSet(1, 0, 0, 0));

			altAxis = XMVector3Normalize(altAxis);
			Ralign = XMMatrixRotationAxis(altAxis, XM_PI);
		}
	}

	XMMATRIX Rroll = XMMatrixRotationAxis(target, mSkyRoll);

	XMMATRIX S = XMMatrixScaling(5000.0f, 5000.0f, 5000.0f);

	XMStoreFloat4x4(&mSkyRitem->World, S * Rroll * Ralign);

	mSkyRitem->NumFramesDirty = gNumFrameResources;
}



void DX12App::LoadTexture(std::string name, std::wstring filename, TextureType type)
{
	auto tex = std::make_unique<Texture>();
	tex->Filename = filename;
	tex->Type = type;
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tex->Filename.c_str(),
		tex->Resource, tex->UploadHeap));
	mTextures[name] = std::move(tex);
}

void DX12App::LoadTextures()
{
	// Defaults
	LoadTexture("black", L"../Textures/black.dds");			 // always in 0 slot
	LoadTexture("diffuse", L"../Textures/white1x1.dds");


	// Last textures for sky
	LoadTexture("skyBrdf", L"../Textures/skyBrdf.dds");
	LoadTexture("skyDiffuseCube", L"../Textures/skyDiffuseCube.dds", TextureType::CUBEMAP);
	LoadTexture("skyIrradianceCube", L"../Textures/skyIrradianceCube.dds", TextureType::CUBEMAP);
}

void DX12App::LoadTerrainTextures()
{

	// 0 = как есть
	// 1 = flip Y (y -> N-1-y)
	// 2 = swap X/Y
	// 3 = swap + flip
	const int TILE_MAPPING_MODE = 1;

	for (int layer = 0; layer < layers; layer++)
	{
		int N = (1 << layer);

		for (int x = 0; x < N; x++)
			for (int y = 0; y < N; y++)
			{
				int fx = x;
				int fy = y;

				switch (TILE_MAPPING_MODE)
				{
				case 0: // no change
					fx = x; fy = y;
					break;

				case 1: // flip Y
					fx = x; fy = (N - 1) - y;
					break;

				case 2: // swap X/Y
					fx = y; fy = x;
					break;

				case 3: // swap + flip 
					fx = y; fy = (N - 1) - x;
					break;
				}

			
				LoadTexture(
					"tile_diffuse_level" + std::to_string(layer) + "_" + std::to_string(x) + "_" + std::to_string(y),
					L"../Textures/Terrain/L" + std::to_wstring(layer) + L"/diffuse/tile_diffuse_level" +
					std::to_wstring(layer) + L"_" + std::to_wstring(fx) + L"_" + std::to_wstring(fy) + L".dds"
				);

				LoadTexture(
					"tile_height_level" + std::to_string(layer) + "_" + std::to_string(x) + "_" + std::to_string(y),
					L"../Textures/Terrain/L" + std::to_wstring(layer) + L"/height/tile_height_level" +
					std::to_wstring(layer) + L"_" + std::to_wstring(fx) + L"_" + std::to_wstring(fy) + L".dds"
				);

				LoadTexture(
					"tile_normal_level" + std::to_string(layer) + "_" + std::to_string(x) + "_" + std::to_string(y),
					L"../Textures/Terrain/L" + std::to_wstring(layer) + L"/normal/tile_normal_level" +
					std::to_wstring(layer) + L"_" + std::to_wstring(fx) + L"_" + std::to_wstring(fy) + L".dds"
				);

				LoadTexture("tile_ao_level" + std::to_string(layer) + "_" + std::to_string(x) + "_" + std::to_string(y),
					L"../Textures/Terrain/L" + std::to_wstring(layer) + L"/ao/tile_ao_level" +
					std::to_wstring(layer) + L"_" + std::to_wstring(x) + L"_" + std::to_wstring(y) + L".dds");

			}
	}
}


void DX12App::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTables[10];
	for (int i = 0; i < 10; i++) {
		texTables[i].Init(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			1,  // number of descriptors
			i); // register ti
	}

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[20];

	// Perfomance TIP: Order from most frequent to least frequent.
	for (int i = 0; i < 10; i++) {
		slotRootParameter[i].InitAsDescriptorTable(1, &texTables[i], D3D12_SHADER_VISIBILITY_ALL); // 0-9   = textures
		slotRootParameter[i + 10].InitAsConstantBufferView(i);									   // 10-19 = CBs
	}

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(20, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature["default"].GetAddressOf())));
}

void DX12App::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 20000;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());


	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	int i = 0;
	mTextures["black"]->SrvHeapIndex = i++;
	auto& tex = mTextures["black"]->Resource;
	srvDesc.Format = tex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, hDescriptor);
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	// texture descriptors except default "black"
	for (auto &Tex : mTextures)
	{
		if (Tex.first == "black") continue;

		Tex.second->SrvHeapIndex = i++;
		auto& tex = Tex.second->Resource;
		srvDesc.Format = tex->GetDesc().Format;

		switch (Tex.second->Type)
		{
		case TextureType::TEXTURE2D:
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
			srvDesc.Texture2D.MipLevels = tex->GetDesc().MipLevels;
			break;
			
		case TextureType::CUBEMAP:
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
			srvDesc.TextureCube.MipLevels = tex->GetDesc().MipLevels;
			break;
		}

		md3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	}

	mGBuffer->Channel0SRVHeapIndex = (UINT)mTextures.size();
	mShadowMapHeapIndex = mGBuffer->Channel0SRVHeapIndex + mGBuffer->NumBuffers + 1;

	// copy gbuffer resources into the srv heap
	auto srvGBuffer = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srvGBuffer.Offset(mGBuffer->Channel0SRVHeapIndex, mCbvSrvDescriptorSize);
	md3dDevice->CopyDescriptorsSimple(mGBuffer->NumBuffers, srvGBuffer,
		mGBuffer->m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void DX12App::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["deferredVS"] = d3dUtil::CompileShader(L"Shaders\\DeferredGeometry.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["displaceVS"] = d3dUtil::CompileShader(L"Shaders\\DeferredGeometry.hlsl", nullptr, "displaceVS", "vs_5_0");
	mShaders["tessVS"] = d3dUtil::CompileShader(L"Shaders\\DeferredGeometry.hlsl", nullptr, "tessVS", "vs_5_0");
	mShaders["tessHS"] = d3dUtil::CompileShader(L"Shaders\\DeferredGeometry.hlsl", nullptr, "HS", "hs_5_0");
	mShaders["tessDS"] = d3dUtil::CompileShader(L"Shaders\\DeferredGeometry.hlsl", nullptr, "DS", "ds_5_0");
	mShaders["curtainsGS"] = d3dUtil::CompileShader(L"Shaders\\DeferredGeometry.hlsl", nullptr, "curtainsGS", "gs_5_0");
	mShaders["deferredPS"] = d3dUtil::CompileShader(L"Shaders\\DeferredGeometry.hlsl", nullptr, "DeferredPS", "ps_5_0");
	mShaders["originalNormalPS"] = d3dUtil::CompileShader(L"Shaders\\DeferredGeometry.hlsl", nullptr, "OriginalNormalPS", "ps_5_0");
	
	mShaders["shadowVS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["shadowGS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["shadowAlphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["deferredLightsVS"] = d3dUtil::CompileShader(L"Shaders\\DeferredLights.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["deferredLightsPS"] = d3dUtil::CompileShader(L"Shaders\\DeferredLights.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["deferredLightsGeometryVS"] = d3dUtil::CompileShader(L"Shaders\\DeferredLights.hlsl", nullptr, "LightsGeometryVS", "vs_5_1");
	mShaders["deferredAmbientPS"] = d3dUtil::CompileShader(L"Shaders\\DeferredLights.hlsl", nullptr, "AmbientPS", "ps_5_1");
	
	mShaders["postVS"] = d3dUtil::CompileShader(L"Shaders\\PostProcessing.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["postPS"] = d3dUtil::CompileShader(L"Shaders\\PostProcessing.hlsl", nullptr, "PS", "ps_5_0");


	mInputLayout =
	{
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void DX12App::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	std::vector<GeometryGenerator::MeshData> allMeshData;

	// if you want to generate new model -- generate it here
	allMeshData.push_back( geoGen.CreateGrid(1.0f, 1.0f, 128, 128, 1.0f) );           // grid
	allMeshData.push_back( geoGen.CreateBox(10.0f, 10.0f, 10.0f, 3) );                // box


	// 
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex and index buffer.
	std::vector<UINT> vertexOffsets;
	vertexOffsets.push_back(0);
	std::vector<UINT> indexOffsets;
	indexOffsets.push_back(0);
	for (size_t i = 1; i < allMeshData.size(); i++)
	{
		vertexOffsets.push_back(vertexOffsets.at(i - 1) + (UINT) allMeshData.at(i - 1).Vertices.size());
		indexOffsets.push_back(indexOffsets.at(i - 1) + (UINT) allMeshData.at(i - 1).Indices32.size());
	}
	
	// generating submeshes
	size_t totalVertexCount = 0;
	std::vector<SubmeshGeometry> allSubmeshes;
	for (size_t i = 0; i < allMeshData.size(); i++)
	{
		SubmeshGeometry submesh;
		auto& mesh = allMeshData.at(i);
		submesh.IndexCount = (UINT)mesh.Indices32.size();
		submesh.StartIndexLocation = indexOffsets.at(i);
		submesh.BaseVertexLocation = vertexOffsets.at(i);

		XMFLOAT3 vMin = { FLT_MAX, FLT_MAX, FLT_MAX };
		XMFLOAT3 vMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

		for (size_t j = 0; j < mesh.Vertices.size(); ++j)
		{
			auto& vertex = mesh.Vertices[j];
			vMin.x = std::min(vMin.x, vertex.Position.x);
			vMin.y = std::min(vMin.y, vertex.Position.y);
			vMin.z = std::min(vMin.z, vertex.Position.z);

			vMax.x = std::max(vMax.x, vertex.Position.x);
			vMax.y = std::max(vMax.y, vertex.Position.y);
			vMax.z = std::max(vMax.z, vertex.Position.z);
		}

		// generating bounding box
		XMFLOAT3 center = {
		  0.5f * (vMin.x + vMax.x),
		  0.5f * (vMin.y + vMax.y),
		  0.5f * (vMin.z + vMax.z)
		};
		XMFLOAT3 extents = {
		  0.5f * (vMax.x - vMin.x),
		  0.5f * (vMax.y - vMin.y),
		  0.5f * (vMax.z - vMin.z)
		};

		BoundingBox box(center, extents);
		submesh.Bounds = box;

		allSubmeshes.push_back(submesh);
		totalVertexCount += mesh.Vertices.size();
	}

	// pack the vertices of all the meshes into one vertex buffer
	std::vector<Vertex> vertices(totalVertexCount);
	UINT k = 0;
	for (GeometryGenerator::MeshData mesh : allMeshData) {
		for (size_t i = 0; i < mesh.Vertices.size(); ++i, ++k)
		{
			vertices[k].Tangent = mesh.Vertices[i].TangentU;
			vertices[k].Pos = mesh.Vertices[i].Position;
			vertices[k].Normal = mesh.Vertices[i].Normal;
			vertices[k].TexC = mesh.Vertices[i].TexC;
		}
	}

	std::vector<std::uint16_t> indices;
	for (GeometryGenerator::MeshData mesh : allMeshData)
		indices.insert(indices.end(), std::begin(mesh.GetIndices16()), std::end(mesh.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	for (size_t i = 0; i < allMeshData.size(); i++)
	{
		geo->DrawArgs[allMeshData.at(i).name] = allSubmeshes.at(i);
	}

	mGeometries[geo->Name] = std::move(geo);
}

void DX12App::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature["default"].Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["deferredVS"]->GetBufferPointer()),
		mShaders["deferredVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		nullptr,
		0
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
#ifdef DEBUG_VIEW
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
#else
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
#endif // DEBUG_VIEW
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	//
	// PSO for shadow map pass.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc;
	ZeroMemory(&smapPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	smapPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	smapPsoDesc.pRootSignature = mRootSignature["default"].Get();
	smapPsoDesc.VS =
	{
	  reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
	  mShaders["shadowVS"]->GetBufferSize()
	};
	smapPsoDesc.GS =
	{
	  reinterpret_cast<BYTE*>(mShaders["shadowGS"]->GetBufferPointer()),
	  mShaders["shadowGS"]->GetBufferSize()
	};
	smapPsoDesc.PS = { nullptr, 0 };

	smapPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	smapPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	smapPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	smapPsoDesc.SampleMask = UINT_MAX;
	smapPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	smapPsoDesc.SampleDesc.Count = 1;
	smapPsoDesc.SampleDesc.Quality = 0;
	smapPsoDesc.DSVFormat = mDepthStencilFormat;
	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	smapPsoDesc.NumRenderTargets = 0;
	smapPsoDesc.RasterizerState.DepthBias = 1000;
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

	//
	// PSO for sky.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;

	// The camera is inside the sky sphere, so just turn off culling.
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.pRootSignature = mRootSignature["default"].Get();
	skyPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};
	skyPsoDesc.DS = { nullptr, 0 };
	skyPsoDesc.HS = { nullptr, 0 };
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

	//
	//	PSO for deferred geometry pass
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredGeometryPsoDesc = opaquePsoDesc;
	deferredGeometryPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["deferredPS"]->GetBufferPointer()),
		mShaders["deferredPS"]->GetBufferSize()
	};
#ifdef DEBUG_VIEW
	deferredGeometryPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
#else
	deferredGeometryPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
#endif // DEBUG_VIEW
	deferredGeometryPsoDesc.NumRenderTargets = 5;
	deferredGeometryPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;		   // diffuse
	deferredGeometryPsoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;    // zwzanashih
	deferredGeometryPsoDesc.RTVFormats[2] = DXGI_FORMAT_R16G16B16A16_SNORM;	   // normal
	deferredGeometryPsoDesc.RTVFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM;        // diffuse albedo
	deferredGeometryPsoDesc.RTVFormats[4] = DXGI_FORMAT_R8G8B8A8_UNORM;        // fresnel & roughness
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredGeometryPsoDesc, IID_PPV_ARGS(&mPSOs["deferredGeometry"])));

	deferredGeometryPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["displaceVS"]->GetBufferPointer()),
		mShaders["displaceVS"]->GetBufferSize()
	};
	deferredGeometryPsoDesc.GS =
	{
	  reinterpret_cast<BYTE*>(mShaders["curtainsGS"]->GetBufferPointer()),
	  mShaders["curtainsGS"]->GetBufferSize()
	};
	deferredGeometryPsoDesc.PS =
	{
	 reinterpret_cast<BYTE*>(mShaders["originalNormalPS"]->GetBufferPointer()),
	 mShaders["originalNormalPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredGeometryPsoDesc, IID_PPV_ARGS(&mPSOs["terrainGeometry"])));

	deferredGeometryPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["tessVS"]->GetBufferPointer()),
		mShaders["tessVS"]->GetBufferSize()
	};
	deferredGeometryPsoDesc.HS =
	{
		reinterpret_cast<BYTE*>(mShaders["tessHS"]->GetBufferPointer()),
		mShaders["tessHS"]->GetBufferSize()
	};
	deferredGeometryPsoDesc.DS =
	{
		reinterpret_cast<BYTE*>(mShaders["tessDS"]->GetBufferPointer()),
		mShaders["tessDS"]->GetBufferSize()
	};
	deferredGeometryPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredGeometryPsoDesc, IID_PPV_ARGS(&mPSOs["tessGeometry"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredPsoDesc = {};
	deferredPsoDesc.InputLayout = { nullptr, 0 };
	deferredPsoDesc.pRootSignature = mRootSignature["default"].Get();
	deferredPsoDesc.VS =
	{
	 reinterpret_cast<BYTE*>(mShaders["deferredLightsVS"]->GetBufferPointer()),
	 mShaders["deferredLightsVS"]->GetBufferSize()
	};
	deferredPsoDesc.PS =
	{
	 reinterpret_cast<BYTE*>(mShaders["deferredLightsPS"]->GetBufferPointer()),
	 mShaders["deferredLightsPS"]->GetBufferSize()
	};
	deferredPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	CD3DX12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	blendDesc.RenderTarget[0].BlendEnable = true;
	blendDesc.RenderTarget[0].LogicOpEnable = false;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	deferredPsoDesc.BlendState = blendDesc;

	deferredPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	deferredPsoDesc.DepthStencilState.DepthEnable = false;
	deferredPsoDesc.DepthStencilState.StencilEnable = false;
	deferredPsoDesc.SampleMask = UINT_MAX;
	deferredPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	deferredPsoDesc.NumRenderTargets = 1;
	deferredPsoDesc.RTVFormats[0] = mBackBufferFormat;
	deferredPsoDesc.SampleDesc.Count = 1;
	deferredPsoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredPsoDesc, IID_PPV_ARGS(&mPSOs["deferredLights"])));

	//
	// PSO for ambient
	//
	deferredPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["deferredAmbientPS"]->GetBufferPointer()),
		mShaders["deferredAmbientPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredPsoDesc, IID_PPV_ARGS(&mPSOs["deferredAmbient"])));

	//
	// PSO for using geometry for lights
	//
	deferredPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	deferredPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
	//deferredPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME; // for debug
	deferredPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
	deferredPsoDesc.DepthStencilState.DepthEnable = true;
	deferredPsoDesc.DepthStencilState.StencilEnable = true;
	deferredPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	deferredPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["deferredLightsGeometryVS"]->GetBufferPointer()),
		mShaders["deferredLightsGeometryVS"]->GetBufferSize()
	};
	deferredPsoDesc.PS =
	{
	 reinterpret_cast<BYTE*>(mShaders["deferredLightsPS"]->GetBufferPointer()),
	 mShaders["deferredLightsPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredPsoDesc, IID_PPV_ARGS(&mPSOs["deferredLightsGeometry"])));

	//
	// PSO for post process
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.pRootSignature = mRootSignature["default"].Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["postVS"]->GetBufferPointer()),
		mShaders["postVS"]->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["postPS"]->GetBufferPointer()),
		mShaders["postPS"]->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = false;
	psoDesc.DepthStencilState.StencilEnable = false;
	psoDesc.DSVFormat = mDepthStencilFormat;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = mBackBufferFormat;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs["PostProcessPSO"])));
}

void DX12App::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			2, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), (UINT)mAllLights.size()));
	}
}

void DX12App::BuildMaterials()
{
	int matCBI = 0;

	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = matCBI++;
	sky->DiffuseSrvHeapIndex = mTextures["skyDiffuseCube"]->SrvHeapIndex;
	sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;

	mMaterials[sky->Name] = std::move(sky);

	for (int i = 0; i < 11; i++)
	{
		for (int j = 0; j < 11; j++)
		{
			auto sphere = std::make_unique<Material>();
			sphere->Name = "sphere_" + std::to_string(i) + "_" + std::to_string(j);
			sphere->MatCBIndex = matCBI++;
			sphere->DiffuseSrvHeapIndex = mTextures["diffuse"]->SrvHeapIndex;
			sphere->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			sphere->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
			sphere->Roughness = j / 10.f;
			sphere->Metallic = i / 10.f;

			mMaterials[sphere->Name] = std::move(sphere);
		}
	}

	// terrain materials
	for (int layer = 0; layer < layers; layer++)
		for (int x = 0; x < (1 << layer); x++)
			for (int y = 0; y < (1 << layer); y++)
			{
				auto terrain = std::make_unique<Material>();
				terrain->Name = "terrain" + std::to_string(layer) + "_" + std::to_string(x) + "_" + std::to_string(y);
				terrain->MatCBIndex = matCBI++;
				terrain->DiffuseSrvHeapIndex = mTextures["tile_diffuse_level" + std::to_string(layer) + "_" + std::to_string(x) + "_" + std::to_string(y)]->SrvHeapIndex;
				terrain->DisplaceSrvHeapIndex = mTextures["tile_height_level" + std::to_string(layer) + "_" + std::to_string(x) + "_" + std::to_string(y)]->SrvHeapIndex;
				terrain->NormalSrvHeapIndex = mTextures["tile_normal_level" + std::to_string(layer) + "_" + std::to_string(x) + "_" + std::to_string(y)]->SrvHeapIndex;
				terrain->AOSrvHeapIndex = mTextures["tile_ao_level" + std::to_string(layer) + "_" + std::to_string(x) + "_" + std::to_string(y)]->SrvHeapIndex;
				terrain->DiffuseAlbedo = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
				terrain->FresnelR0 = XMFLOAT3(0.5f, 0.5f, 0.5f);
				terrain->Roughness = 1.0f;
				terrain->Metallic = 0.1f;

				mMaterials[terrain->Name] = std::move(terrain);
			}
}

RenderItem* DX12App::BuildRenderItem(std::string name, std::string material, XMMATRIX translate, std::vector<std::string>* LODGeoNames, int layer, float scale, float scaleTex)
{
	auto ptr = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&ptr->World, XMMatrixScaling(scale, scale, scale) * translate);
	XMStoreFloat4x4(&ptr->TexTransform, XMMatrixScaling(scaleTex, scaleTex, scaleTex));
	ptr->ObjCBIndex = ObjCBIndex++;
	ptr->Mat = mMaterials[material].get();
	ptr->Geo = mGeometries["shapeGeo"].get();
	ptr->geoName = name;
	ptr->IndexCount = ptr->Geo->DrawArgs[name].IndexCount;
	ptr->Geo->DrawArgs[name].Bounds.Transform(ptr->Bounds, XMLoadFloat4x4(&ptr->World));
	ptr->StartIndexLocation = ptr->Geo->DrawArgs[name].StartIndexLocation;
	ptr->BaseVertexLocation = ptr->Geo->DrawArgs[name].BaseVertexLocation;
	ptr->layer = layer;
	if (LODGeoNames != nullptr)
		ptr->LODGeoNames = *LODGeoNames;

	auto* res = ptr.get();
	mRitemLayer[layer].push_back(res);
	mAllRitems.push_back(std::move(ptr));

	return res;
}

void DX12App::BuildRenderItems()
{
	mSkyRitem = BuildRenderItem("box", "sky", XMMatrixIdentity(), nullptr, (int)RenderLayer::Sky, 5000.0f);


}

void DX12App::BuildLightObjects()
{
	auto dir1 = std::make_unique<LightObject>();
	dir1->LightType = LightType::Directional;
	dir1->Strength = { 1.f, 1.f, 1.f };
	dir1->Direction = { 0.57735f, -0.57735f, 0.57735f };
	mAllLights.push_back(std::move(dir1));


	auto srvCpuStart = mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	auto srvGpuStart = mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	auto dsvCpuStart = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

	for (size_t i = 0; i < mAllLights.size(); i++)
	{
		switch (mAllLights.at(i)->LightType)
		{
		case LightType::Directional:
			break;

		case LightType::Pointlight:
			mAllLights.at(i)->GeoName = "sphere";
			break;

		case LightType::Spotlight:
			mAllLights.at(i)->GeoName = "cone";
			break;
		}

		mAllLights.at(i)->lightCBIndex = i;
		mAllLights.at(i)->shadowMap = new ShadowMap(md3dDevice.Get(), 2048, 2048);

		mAllLights.at(i)->shadowMap->BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mShadowMapHeapIndex + i, mCbvSrvDescriptorSize),
			CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mShadowMapHeapIndex + i, mCbvSrvDescriptorSize),
			CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvCpuStart, 1 + i, mDsvDescriptorSize));
	}
}

void DX12App::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// For each render item...
	for (const auto& ri : ritems)
	{
		Material* mat = ri->Mat;
		UINT textureIndex = mat->DiffuseSrvHeapIndex;
		UINT normalIndex = mat->NormalSrvHeapIndex;
		UINT displaceIndex = mat->DisplaceSrvHeapIndex;
		UINT aoIndex = mat->AOSrvHeapIndex;

		// register texture in t0
		CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			textureIndex,  // Смещение в куче дескрипторов
			mCbvSrvDescriptorSize
		);
		cmdList->SetGraphicsRootDescriptorTable(0, texHandle);

		// register texture in t1
		CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle1(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			normalIndex,  // Смещение в куче дескрипторов
			mCbvSrvDescriptorSize
		);
		cmdList->SetGraphicsRootDescriptorTable(1, texHandle1);

		// register texture in t2
		CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle2(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			displaceIndex,  // Смещение в куче дескрипторов
			mCbvSrvDescriptorSize
		);
		cmdList->SetGraphicsRootDescriptorTable(2, texHandle2);

		CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle4(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			aoIndex,
			mCbvSrvDescriptorSize
		);
		cmdList->SetGraphicsRootDescriptorTable(4, texHandle4);

		CD3DX12_GPU_DESCRIPTOR_HANDLE paintHandle(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			mPaintMaskSrvIndex,
			mCbvSrvDescriptorSize
		);
		cmdList->SetGraphicsRootDescriptorTable(5, paintHandle);


		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(10, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(12, matCBAddress);

		if (ri->LODGeoNames.empty())
			cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
		else if (ri->currentLOD < ri->LODGeoNames.size())
		{
			auto &item = ri->Geo->DrawArgs[ri->LODGeoNames.at(ri->currentLOD)];
			cmdList->DrawIndexedInstanced(item.IndexCount, 1, item.StartIndexLocation, item.BaseVertexLocation, 0);
		}
		else
		{
			auto& item = ri->Geo->DrawArgs[ri->LODGeoNames.back()];
			cmdList->DrawIndexedInstanced(item.IndexCount, 1, item.StartIndexLocation, item.BaseVertexLocation, 0);
		}
	}
}

void DX12App::DrawDeferredGeometry()
{
	auto passCB = mCurrFrameResource->PassCB->Resource();
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);
	mCommandList->SetPipelineState(mPSOs["deferredGeometry"].Get());

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[5] = {
		 mGBuffer->DiffuseRTV,
		 mGBuffer->ZWzanashihRTV,
		 mGBuffer->NormalRTV,
		 mGBuffer->MaterialAlbedoRTV,
		 mGBuffer->MaterialFresnelRoughnessRTV
	};
	mCommandList->OMSetRenderTargets(5, rtvs, false, &DepthStencilView());

	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	mCommandList->SetGraphicsRootConstantBufferView(11, passCB->GetGPUVirtualAddress());

	mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DrawRenderItems(mCommandList.Get(), mVisibleRitems[(int)RenderLayer::Opaque]);

	// terrain w/ tessellation draw
	mCommandList->SetPipelineState(mPSOs["terrainGeometry"].Get());
	DrawRenderItems(mCommandList.Get(), mVisibleTerrain);
	
	for (int i = 0; i < (int)RenderLayer::Count; i++)
	{
		mVisibleRitems[i].clear();
	}
}

void DX12App::DrawDeferredLights()
{
	auto passCB = mCurrFrameResource->PassCB->Resource();

	UINT lightCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(LightConstants));
	auto lightCB = mCurrFrameResource->LightCB->Resource();

	mCommandList->SetGraphicsRootConstantBufferView(10, passCB->GetGPUVirtualAddress());
	mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);
	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &mGBuffer->BloomRTV, true, &DepthStencilView());


	for (int i = 0; i < 5; i++)
	{
		// register texture
		CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			mGBuffer->Channel0SRVHeapIndex + i,
			mCbvSrvDescriptorSize
		);
		mCommandList->SetGraphicsRootDescriptorTable(i + 1, texHandle);
	}

	// sky diffuse
	mCommandList->SetGraphicsRootDescriptorTable(6, CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		mTextures["skyDiffuseCube"]->SrvHeapIndex,
		mCbvSrvDescriptorSize
	));
	// sky irradiance
	mCommandList->SetGraphicsRootDescriptorTable(7, CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		mTextures["skyIrradianceCube"]->SrvHeapIndex,
		mCbvSrvDescriptorSize
	));
	// sky brdf
	mCommandList->SetGraphicsRootDescriptorTable(8, CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		mTextures["skyBrdf"]->SrvHeapIndex,
		mCbvSrvDescriptorSize
	));



	for (auto& Light : mAllLights)
	{
		auto shadowMap = Light->shadowMap;
		mCommandList->SetGraphicsRootDescriptorTable(0, shadowMap->Srv());

		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + Light->lightCBIndex * lightCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(11, lightCBAddress);

		if (Light->LightType == LightType::Directional)
		{
			mCommandList->SetPipelineState(mPSOs["deferredLights"].Get());
			mCommandList->DrawInstanced(6, 1, 0, 0);
		}
		else
		{
			mCommandList->SetPipelineState(mPSOs["deferredLightsGeometry"].Get());

			mCommandList->IASetVertexBuffers(0, 1, &mGeometries["shapeGeo"]->VertexBufferView());
			mCommandList->IASetIndexBuffer(&mGeometries["shapeGeo"]->IndexBufferView());

			mCommandList->DrawIndexedInstanced(mGeometries["shapeGeo"]->DrawArgs[Light->GeoName].IndexCount, 1,
				mGeometries["shapeGeo"]->DrawArgs[Light->GeoName].StartIndexLocation,
				mGeometries["shapeGeo"]->DrawArgs[Light->GeoName].BaseVertexLocation, 0);
		}
	}

	mCommandList->SetPipelineState(mPSOs["deferredAmbient"].Get());
	mCommandList->DrawInstanced(6, 1, 0, 0); // todo 3?
}

void DX12App::DrawSkyBox()
{
	mCommandList->SetPipelineState(mPSOs["sky"].Get());
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(11, passCB->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);
}

void DX12App::DrawPostProcess()
{
	// register input texture
	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		mGBuffer->Channel0SRVHeapIndex + 6,
		mCbvSrvDescriptorSize
	);
	mCommandList->SetGraphicsRootDescriptorTable(0, texHandle);

	// register depth texture
	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle1(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		mGBuffer->Channel0SRVHeapIndex + 1, //zw
		mCbvSrvDescriptorSize
	);
	mCommandList->SetGraphicsRootDescriptorTable(1, texHandle1);

	// register normal texture
	CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle2(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		mGBuffer->Channel0SRVHeapIndex + 2, // normal
		mCbvSrvDescriptorSize
	);
	mCommandList->SetGraphicsRootDescriptorTable(2, texHandle2);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	auto postProcessCB = mCurrFrameResource->PostProcessCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(10,
		postProcessCB->GetGPUVirtualAddress()); // PostProcess Settings
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(11, passCB->GetGPUVirtualAddress()); // b1

	mCommandList->SetPipelineState(mPSOs["PostProcessPSO"].Get());
	mCommandList->DrawInstanced(3, 1, 0, 0);
}

void DX12App::DrawShadowMaps()
{
	auto passCB = mCurrFrameResource->PassCB->Resource();


	UINT lightCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(LightConstants));
	auto lightCB = mCurrFrameResource->LightCB->Resource();

	mCommandList->SetGraphicsRootConstantBufferView(11, passCB->GetGPUVirtualAddress());
	mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());

	for (auto &Light : mAllLights)
	{
		auto shadowMap = Light->shadowMap;
		mCommandList->RSSetViewports(1, &shadowMap->Viewport());
		mCommandList->RSSetScissorRects(1, &shadowMap->ScissorRect());

		// Transition render target to dsv
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			shadowMap->Resource(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_DEPTH_WRITE));
		// Clear depth stencil
		mCommandList->ClearDepthStencilView(shadowMap->Dsv(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		// Specify the buffers we are going to render to.
		mCommandList->OMSetRenderTargets(0, nullptr, true, &shadowMap->Dsv());


		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + Light->lightCBIndex * lightCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(13, lightCBAddress);

		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

		// Transition dsv to rtv
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			shadowMap->Resource(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_GENERIC_READ));
	}
}

Node* DX12App::BuildNode(int layer, float x, float y, int xi, int yi)
{

	Node* node = new Node();
	node->layer = layer;

	float scaleFactor = RootSize / ( 1 << layer );

	std::string debugString = std::to_string(layer) + "_" + std::to_string(xi) + "_" + std::to_string(yi) + "\n";
	OutputDebugStringA(debugString.c_str());

	node->RItem = BuildRenderItem("grid", "terrain" + std::to_string(layer) + "_" + std::to_string(xi) + "_" + std::to_string(yi),
		XMMatrixScaling(scaleFactor, 1.0f, scaleFactor) * XMMatrixTranslation(x, -40.f, y),
		nullptr, (int)RenderLayer::Terrain);
	node->RItem->Bounds.Center.y += 150.0f;
	node->RItem->Bounds.Extents.y += 400.0f;

	if (layer > layers - 2)
	{
		return node;
	}

	node->hasChildren = true;


	XMFLOAT2 offsets[4] = { {-1.f, -1.f},
							{-1.f, 1.f},
							{1.f, -1.f},
							{1.f, 1.f} };

	int coords[4][2] =		{{0, 1},
							{0, 0},
							{1, 1},
							{1, 0} };

	for (int i = 0; i < 4; i++)
	{
		node->children[i] = BuildNode(layer + 1, x + offsets[i].x * scaleFactor * 0.25f, y + offsets[i].y * scaleFactor * 0.25f,
					xi * 2 + coords[i][0], yi * 2 + coords[i][1]);
	}

	return node;
}

void DX12App::BuildTerrainQuadTree()
{
	root = BuildNode(0, 0.f, 0.f, 0, 0);
}

void DX12App::UpdateVisibleTerrainTiles()
{
	mVisibleTerrain.clear();
	
	ChooseVisibleTerrainTile(root);

	char buf[256];
	sprintf_s(buf, "Visible Opaque: %d | Visible Terrain: %d\n",
		(int)mVisibleRitems[(int)RenderLayer::Opaque].size(),
		(int)mVisibleTerrain.size());
	OutputDebugStringA(buf);


}

void DX12App::ChooseVisibleTerrainTile(Node* node)
{
	if (!mCamera.Bounds.Intersects(node->RItem->Bounds))
		return;

	float distToCam;
	XMStoreFloat(&distToCam, XMVector3Length(XMVectorSubtract(mCamera.GetPosition(), XMLoadFloat3(&node->RItem->Bounds.Center))));

	if (distToCam > thresholds[node->layer] || !node->hasChildren)
		mVisibleTerrain.push_back(node->RItem);

	else
	{
		for (auto chold : node->children)
		{
			ChooseVisibleTerrainTile(chold);
		}
	}
}

void DX12App::BuildPaintMask()
{
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = PaintW;
	texDesc.Height = PaintH;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(mPaintMask.GetAddressOf())
	));

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mPaintMaskSrvIndex,
		mCbvSrvDescriptorSize
	);
	md3dDevice->CreateShaderResourceView(mPaintMask.Get(), &srvDesc, srvHandle);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R8_UNORM;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mPaintMaskUavIndex,
		mCbvSrvDescriptorSize
	);
	md3dDevice->CreateUnorderedAccessView(mPaintMask.Get(), nullptr, &uavDesc, uavHandle);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mPaintMask.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	CD3DX12_CPU_DESCRIPTOR_HANDLE uavCpu(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mPaintMaskUavIndex,
		mCbvSrvDescriptorSize
	);
	CD3DX12_GPU_DESCRIPTOR_HANDLE uavGpu(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		mPaintMaskUavIndex,
		mCbvSrvDescriptorSize
	);

	ID3D12DescriptorHeap* heaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(1, heaps);

	UINT clear[4] = { 0,0,0,0 };
	mCommandList->ClearUnorderedAccessViewUint(uavGpu, uavCpu, mPaintMask.Get(), clear, 0, nullptr);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mPaintMask.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}


void DX12App::BuildPaintCompute()
{
	auto cs = d3dUtil::CompileShader(L"Shaders\\PaintCS.hlsl", nullptr, "main", "cs_5_1");

	CD3DX12_DESCRIPTOR_RANGE uavRange;
	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0

	CD3DX12_ROOT_PARAMETER params[2];
	params[0].InitAsConstantBufferView(0);          // b0
	params[1].InitAsDescriptorTable(1, &uavRange);  // u0

	CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init(2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> blob, err;
	ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err));
	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0, blob->GetBufferPointer(), blob->GetBufferSize(),
		IID_PPV_ARGS(mPaintRootSig.GetAddressOf())
	));

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = mPaintRootSig.Get();
	psoDesc.CS = { reinterpret_cast<BYTE*>(cs->GetBufferPointer()), cs->GetBufferSize() };

	ThrowIfFailed(md3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(mPaintPSO.GetAddressOf())));
}

void DX12App::ExecutePaintStrokes()
{
	if (mPendingStrokes.empty()) return;

	// PaintMask -> UAV
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mPaintMask.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	ID3D12DescriptorHeap* heaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(1, heaps);

	mCommandList->SetPipelineState(mPaintPSO.Get());
	mCommandList->SetComputeRootSignature(mPaintRootSig.Get());

	for (auto& s : mPendingStrokes)
	{
		float u, v;
		if (!PickTerrainUV((int)s.sx, (int)s.sy, u, v))
			continue;

		PaintParamsCB paintCB = {};
		paintCB.CenterUV = { u, v };
		paintCB.RadiusPx = s.radius;
		paintCB.Strength = s.strength;
		paintCB.TexSize = { (float)PaintW, (float)PaintH };

		mPaintParamsCB->CopyData(0, paintCB);

	
		mCommandList->SetComputeRootConstantBufferView(
			0, mPaintParamsCB->Resource()->GetGPUVirtualAddress());

		mCommandList->SetComputeRootDescriptorTable(
			1,
			CD3DX12_GPU_DESCRIPTOR_HANDLE(
				mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
				mPaintMaskUavIndex,
				mCbvSrvDescriptorSize));

		UINT groupsX = (PaintW + 7) / 8;
		UINT groupsY = (PaintH + 7) / 8;
		mCommandList->Dispatch(groupsX, groupsY, 1);

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(mPaintMask.Get()));
	}

	mPendingStrokes.clear();

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mPaintMask.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}


bool DX12App::RayTriangleIntersect(
	const XMVECTOR& rayOrigin,
	const XMVECTOR& rayDir,
	const XMVECTOR& v0,
	const XMVECTOR& v1,
	const XMVECTOR& v2,
	float& t, float& u, float& v)
{
	// Möller–Trumbore
	const float EPS = 1e-6f;

	XMVECTOR e1 = v1 - v0;
	XMVECTOR e2 = v2 - v0;

	XMVECTOR p = XMVector3Cross(rayDir, e2);
	float det = XMVectorGetX(XMVector3Dot(e1, p));
	if (fabsf(det) < EPS) return false;

	float invDet = 1.0f / det;
	XMVECTOR s = rayOrigin - v0;

	u = XMVectorGetX(XMVector3Dot(s, p)) * invDet;
	if (u < 0.0f || u > 1.0f) return false;

	XMVECTOR q = XMVector3Cross(s, e1);

	v = XMVectorGetX(XMVector3Dot(rayDir, q)) * invDet;
	if (v < 0.0f || (u + v) > 1.0f) return false;

	t = XMVectorGetX(XMVector3Dot(e2, q)) * invDet;
	return t > EPS;
}


bool DX12App::PickTerrainUV(int sx, int sy, float& outU, float& outV)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();
	XMMATRIX worldI = XMMatrixIdentity();

	XMVECTOR nearP = XMVector3Unproject(
		XMVectorSet((float)sx, (float)sy, 0.0f, 1.0f),
		mScreenViewport.TopLeftX, mScreenViewport.TopLeftY,
		mScreenViewport.Width, mScreenViewport.Height,
		mScreenViewport.MinDepth, mScreenViewport.MaxDepth,
		proj, view, worldI);

	XMVECTOR farP = XMVector3Unproject(
		XMVectorSet((float)sx, (float)sy, 1.0f, 1.0f),
		mScreenViewport.TopLeftX, mScreenViewport.TopLeftY,
		mScreenViewport.Width, mScreenViewport.Height,
		mScreenViewport.MinDepth, mScreenViewport.MaxDepth,
		proj, view, worldI);

	XMVECTOR rayOrigin = nearP;
	XMVECTOR rayDir = XMVector3Normalize(farP - nearP);

	auto geo = mGeometries["shapeGeo"].get();
	auto& grid = geo->DrawArgs["grid"];

	Vertex* verts = (Vertex*)geo->VertexBufferCPU->GetBufferPointer();
	uint16_t* inds = (uint16_t*)geo->IndexBufferCPU->GetBufferPointer();

	float bestT = FLT_MAX;
	bool hit = false;
	float bestU = 0.0f, bestV = 0.0f;

	for (auto* ri : mVisibleTerrain)
	{
		XMMATRIX W = XMLoadFloat4x4(&ri->World);

		for (UINT i = 0; i < grid.IndexCount; i += 3)
		{
			uint16_t i0 = inds[grid.StartIndexLocation + i + 0] + grid.BaseVertexLocation;
			uint16_t i1 = inds[grid.StartIndexLocation + i + 1] + grid.BaseVertexLocation;
			uint16_t i2 = inds[grid.StartIndexLocation + i + 2] + grid.BaseVertexLocation;

			XMVECTOR p0 = XMVector3TransformCoord(XMLoadFloat3(&verts[i0].Pos), W);
			XMVECTOR p1 = XMVector3TransformCoord(XMLoadFloat3(&verts[i1].Pos), W);
			XMVECTOR p2 = XMVector3TransformCoord(XMLoadFloat3(&verts[i2].Pos), W);

			float t, u, v;
			if (!RayTriangleIntersect(rayOrigin, rayDir, p0, p1, p2, t, u, v))
				continue;

			if (t < bestT)
			{
				bestT = t;
				hit = true;

				XMFLOAT2 uv0 = verts[i0].TexC;
				XMFLOAT2 uv1 = verts[i1].TexC;
				XMFLOAT2 uv2 = verts[i2].TexC;

				float w = 1.0f - u - v;
				float localU = uv0.x * w + uv1.x * u + uv2.x * v;
				float localV = uv0.y * w + uv1.y * u + uv2.y * v;

				XMVECTOR hitPos = rayOrigin + rayDir * bestT;
				float hx = XMVectorGetX(hitPos);
				float hz = XMVectorGetZ(hitPos);

				outU = (hx / RootSize) + 0.5f;
				outV = (hz / RootSize) + 0.5f;
			}
		}
	}

	if (!hit) return false;

	return (outU >= 0.0f && outU <= 1.0f && outV >= 0.0f && outV <= 1.0f);
}


std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> DX12App::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp,
		shadow};
}

