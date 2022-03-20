#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"

#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

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
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTested,
	AlphaTestedTreeSprites,
	Count
};

class TreeBillboardsApp : public D3DApp
{
public:
    TreeBillboardsApp(HINSTANCE hInstance);
    TreeBillboardsApp(const TreeBillboardsApp& rhs) = delete;
    TreeBillboardsApp& operator=(const TreeBillboardsApp& rhs) = delete;
    ~TreeBillboardsApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt); 

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayouts();
    void BuildLandGeometry();
	void BuildUnderLandGeometry();
    void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildPyramidGeometry();
	void BuildCylinderGeometry();
	void BuildConeGeometry();
	void BuildWedgeGeometry();
	void BuildDiamondGeometry();
	void BuildTreeSpritesGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

    float GetHillsHeight(float x, float z)const;
    XMFLOAT3 GetHillsNormal(float x, float z)const;

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mStdInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

    RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = XM_PIDIV2 - 0.1f;
    float mRadius = 75.0f;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        TreeBillboardsApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

TreeBillboardsApp::TreeBillboardsApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

TreeBillboardsApp::~TreeBillboardsApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TreeBillboardsApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);
 
	LoadTextures();
    BuildRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayouts();
    BuildLandGeometry();
	BuildUnderLandGeometry();
    BuildWavesGeometry();
	BuildBoxGeometry();
	BuildPyramidGeometry();
	BuildCylinderGeometry();
	BuildConeGeometry();
	BuildDiamondGeometry();
	BuildWedgeGeometry();
	BuildTreeSpritesGeometry();
	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}
 
void TreeBillboardsApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void TreeBillboardsApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
    UpdateWaves(gt);
}

void TreeBillboardsApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

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

void TreeBillboardsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void TreeBillboardsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void TreeBillboardsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.2f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void TreeBillboardsApp::OnKeyboardInput(const GameTimer& gt)
{
}
 
void TreeBillboardsApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void TreeBillboardsApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if(tu >= 1.0f)
		tu -= 1.0f;

	if(tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void TreeBillboardsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void TreeBillboardsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void TreeBillboardsApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

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
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.75f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 45.0f, 2.0f, 0.0f };
	mMainPassCB.Lights[0].Strength = { 0.025f, 0.010f, 0.005f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void TreeBillboardsApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	//static float t_base = 0.0f;
	//if((mTimer.TotalTime() - t_base) >= 0.25f)
	//{
	//	t_base += 0.25f;

	//	int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
	//	int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

	//	float r = MathHelper::RandF(0.2f, 0.5f);

	//	mWaves->Disturb(i, j, r);
	//}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for(int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);
		
		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	//Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void TreeBillboardsApp::LoadTextures()
{
	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	//grassTex->Filename = L"../../Textures/grass.dds";
	grassTex->Filename = L"../../A2_Sarras_Asper/A2_Sarras_Asper/Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"../../A2_Sarras_Asper/A2_Sarras_Asper/Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	auto fenceTex = std::make_unique<Texture>();
	fenceTex->Name = "fenceTex";
	fenceTex->Filename = L"../../A2_Sarras_Asper/A2_Sarras_Asper/Textures/bricks3.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), fenceTex->Filename.c_str(),
		fenceTex->Resource, fenceTex->UploadHeap));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"../../A2_Sarras_Asper/A2_Sarras_Asper/Textures/treeArray.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));

	//Walls
	auto castleTex = std::make_unique<Texture>();
	castleTex->Name = "castleTex";
	castleTex->Filename = L"../../A2_Sarras_Asper/A2_Sarras_Asper/Textures/bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), castleTex->Filename.c_str(),
		castleTex->Resource, castleTex->UploadHeap));

	auto dirtTex = std::make_unique<Texture>();
	dirtTex->Name = "dirtTex";
	dirtTex->Filename = L"../../A2_Sarras_Asper/A2_Sarras_Asper/Textures/dirt.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), dirtTex->Filename.c_str(),
		dirtTex->Resource, dirtTex->UploadHeap));

	auto windowTex = std::make_unique<Texture>();
	windowTex->Name = "windowTex";
	windowTex->Filename = L"../../A2_Sarras_Asper/A2_Sarras_Asper/Textures/window.dds";
	//castleTex->Filename = L"../../Textures/WireFence.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), windowTex->Filename.c_str(),
		windowTex->Resource, windowTex->UploadHeap));

	//Castle
	auto castle2Tex = std::make_unique<Texture>();
	castle2Tex->Name = "castle2Tex";
	castle2Tex->Filename = L"../../A2_Sarras_Asper/A2_Sarras_Asper/Textures/Castle.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), castle2Tex->Filename.c_str(),
		castle2Tex->Resource, castle2Tex->UploadHeap));

	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[fenceTex->Name] = std::move(fenceTex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
	mTextures[castleTex->Name] = std::move(castleTex);
	mTextures[dirtTex->Name] = std::move(dirtTex);
	mTextures[windowTex->Name] = std::move(windowTex);
	mTextures[castle2Tex->Name] = std::move(castle2Tex);
}

void TreeBillboardsApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0);
    slotRootParameter[2].InitAsConstantBufferView(1);
    slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void TreeBillboardsApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 7;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto fenceTex = mTextures["fenceTex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;
	auto castleTex = mTextures["castleTex"]->Resource;
	auto dirtTex = mTextures["dirtTex"]->Resource;
	auto windowTex = mTextures["windowTex"]->Resource;
	auto castle2Tex = mTextures["castle2Tex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = fenceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = castleTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(castleTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = dirtTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(dirtTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = windowTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(windowTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = castle2Tex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(castle2Tex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);

	//// next descriptor
	//hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	//srvDesc.Format = castleTex->GetDesc().Format;
	//md3dDevice->CreateShaderResourceView(castleTex.Get(), &srvDesc, hDescriptor);
}

void TreeBillboardsApp::BuildShadersAndInputLayouts()
{
	// Define "FOG" on the empties "" for allowing fog
	const D3D_SHADER_MACRO defines[] =
	{
		"", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_1");
	
	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mStdInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void TreeBillboardsApp::BuildLandGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(80.0f,80.0f, 50, 50);

    //
    // Extract the vertex elements we are interested and apply the height function to
    // each vertex.  In addition, color the vertices based on their height so we have
    // sandy looking beaches, grassy low hills, and snow mountain peaks.
    //

    std::vector<Vertex> vertices(grid.Vertices.size());
    for(size_t i = 0; i < grid.Vertices.size(); ++i)
    {
        auto& p = grid.Vertices[i].Position;
        vertices[i].Pos = p;
        vertices[i].Pos.y =p.y + 0.1f;
		//vertices[i].Pos.y = p.y - 0.3f;
        //vertices[i].Normal = GetHillsNormal(p.x, p.z);
		vertices[i].Normal = grid.Vertices[i].Normal;
		vertices[i].TexC = grid.Vertices[i].TexC;
    }

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    std::vector<std::uint16_t> indices = grid.GetIndices16();
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildUnderLandGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData gridU = geoGen.CreateGrid(128.0f, 128.0f, 50, 50);

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  In addition, color the vertices based on their height so we have
	// sandy looking beaches, grassy low hills, and snow mountain peaks.
	//

	std::vector<Vertex> vertices(gridU.Vertices.size());
	for (size_t i = 0; i < gridU.Vertices.size(); ++i)
	{
		auto& p = gridU.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = p.y - 0.51f;
		//vertices[i].Pos.y = p.y - 0.3f;
		//vertices[i].Normal = GetHillsNormal(p.x, p.z);
		vertices[i].Normal = gridU.Vertices[i].Normal;
		vertices[i].TexC = gridU.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = gridU.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landUGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landUGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildWavesGeometry()
{
    std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

    // Iterate over each quad.
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for(int i = 0; i < m - 1; ++i)
    {
        for(int j = 0; j < n - 1; ++j)
        {
            indices[k] = i*n + j;
            indices[k + 1] = i*n + j + 1;
            indices[k + 2] = (i + 1)*n + j;

            indices[k + 3] = (i + 1)*n + j;
            indices[k + 4] = i*n + j + 1;
            indices[k + 5] = (i + 1)*n + j + 1;

            k += 6; // next quad
        }
    }

	UINT vbByteSize = mWaves->VertexCount()*sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size()*sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildBoxGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);

	std::vector<Vertex> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		auto& p = box.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "boxGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["box"] = submesh;

	mGeometries["boxGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildPyramidGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f, 0);

	std::vector<Vertex> vertices(pyramid.Vertices.size());
	for (size_t i = 0; i < pyramid.Vertices.size(); ++i)
	{
		auto& p = pyramid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = pyramid.Vertices[i].Normal;
		vertices[i].TexC = pyramid.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = pyramid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "pyramidGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["pyramid"] = submesh;

	mGeometries["pyramidGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildCylinderGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	std::vector<Vertex> vertices(cylinder.Vertices.size());
	for (size_t i = 0; i < cylinder.Vertices.size(); ++i)
	{
		auto& p = cylinder.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = cylinder.Vertices[i].Normal;
		vertices[i].TexC = cylinder.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = cylinder.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "cylinderGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["cylinder"] = submesh;

	mGeometries["cylinderGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildConeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData cone = geoGen.CreateCylinder(0.5f, 0.0f, 3.0f, 20, 20);

	std::vector<Vertex> vertices(cone.Vertices.size());
	for (size_t i = 0; i < cone.Vertices.size(); ++i)
	{
		auto& p = cone.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = cone.Vertices[i].Normal;
		vertices[i].TexC = cone.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = cone.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "coneGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["cone"] = submesh;

	mGeometries["coneGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildWedgeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f, 0);

	std::vector<Vertex> vertices(wedge.Vertices.size());
	for (size_t i = 0; i < wedge.Vertices.size(); ++i)
	{
		auto& p = wedge.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = wedge.Vertices[i].Normal;
		vertices[i].TexC = wedge.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = wedge.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "wedgeGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["wedge"] = submesh;

	mGeometries["wedgeGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildDiamondGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.0f, 1.0f, 1.0f, 0);

	std::vector<Vertex> vertices(diamond.Vertices.size());
	for (size_t i = 0; i < diamond.Vertices.size(); ++i)
	{
		auto& p = diamond.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = diamond.Vertices[i].Normal;
		vertices[i].TexC = diamond.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = diamond.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "diamondGeo";

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["diamond"] = submesh;

	mGeometries["diamondGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildTreeSpritesGeometry()
{
	//step5
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	//static const int treeCount = 16;
	//std::array<TreeSpriteVertex, 16> vertices;
	//for(UINT i = 0; i < treeCount; ++i)
	//{
	//	float x = MathHelper::RandF(-45.0f, 45.0f);
	//	float z = MathHelper::RandF(-45.0f, 45.0f);
	//	float y = 0.0f;
	//	//float y = GetHillsHeight(x, z);

	//	// Move tree slightly above land height.
	//	y += 8.0f;

	//	vertices[i].Pos = XMFLOAT3(x, y, z);
	//	vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
	//}

	//std::array<std::uint16_t, 16> indices =
	//{
	//	0, 1, 2, 3, 4, 5, 6, 7,
	//	8, 9, 10, 11, 12, 13, 14, 15
	//};

	static const int treeCount = 9;
	std::array<TreeSpriteVertex, 9> vertices;
	for (UINT i = 0; i < treeCount; ++i)
	{
		//float x = MathHelper::RandF(-45.0f, 45.0f);
		//float z = MathHelper::RandF(-45.0f, 45.0f);
		float x = 0.0f;
		float z = 0.0f;
		float y = 8.0f;
		//float y = GetHillsHeight(x, z);
		// Move tree slightly above land height.
		//y += 8.0f;


		if (i == 0)
		{
			x = 6.0f;
			z = -30.0f;
		}

		else if (i == 1)
		{
			x = 6.0f;
			z = -22.0f;
		}

		else if (i == 2)
		{
			x = 6.0f;
			z = -14.0f;
		}

		else if (i == 3)
		{
			x = -6.0f;
			z = -30.0f;
		}

		else if (i == 4)
		{
			x = -6.0f;
			z = -22.0f;
		}

		else if (i == 5)
		{
			x = -6.0f;
			z = -14.0f;
		}

		else if (i == 6)
		{
			x = 0.0f;
			z = 12.0f;
		}

		else if (i == 7)
		{
			x = 10.0f;
			z = 12.0f;
		}

		else if (i == 8)
		{
			x = -10.0f;
			z = 12.0f;
		}

		vertices[i].Pos = XMFLOAT3(x, y, z);
		vertices[i].Size = XMFLOAT2(15.0f, 15.0f);
	}

	std::array<std::uint16_t, 16> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7, 8/*, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11*/
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mStdInputLayout.data(), (UINT)mStdInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;

	//there is abug with F2 key that is supposed to turn on the multisampling!
//Set4xMsaaState(true);
	//m4xMsaaState = true;

	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	//
	// PSO for transparent objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	//transparentPsoDesc.BlendState.AlphaToCoverageEnable = true;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	//
	// PSO for alpha tested objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));

	//
	// PSO for tree sprites
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	treeSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	//step1
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}

void TreeBillboardsApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void TreeBillboardsApp::BuildMaterials()
{
	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefence";
	wirefence->MatCBIndex = 2;
	wirefence->DiffuseSrvHeapIndex = 2;
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	wirefence->Roughness = 0.25f;

	//Walls
	auto castle = std::make_unique<Material>();
	castle->Name = "castle";
	castle->MatCBIndex = 3;
	castle->DiffuseSrvHeapIndex = 3;
	castle->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	castle->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	castle->Roughness = 0.25f;

	auto dirt = std::make_unique<Material>();
	dirt->Name = "dirt";
	dirt->MatCBIndex = 4;
	dirt->DiffuseSrvHeapIndex = 4;
	dirt->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	dirt->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	dirt->Roughness = 0.25f;

	auto window = std::make_unique<Material>();
	window->Name = "window";
	window->MatCBIndex = 5;
	window->DiffuseSrvHeapIndex = 5;
	window->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	window->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	window->Roughness = 0.25f;

	//Castle
	auto castle2 = std::make_unique<Material>();
	castle2->Name = "castle2";
	castle2->MatCBIndex = 6;
	castle2->DiffuseSrvHeapIndex = 6;
	castle2->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	castle2->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	castle2->Roughness = 0.25f;

	//last
	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = 7;
	treeSprites->DiffuseSrvHeapIndex = 7;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;

	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["wirefence"] = std::move(wirefence);
	mMaterials["treeSprites"] = std::move(treeSprites);
	mMaterials["castle"] = std::move(castle);
	mMaterials["dirt"] = std::move(dirt);
	mMaterials["window"] = std::move(window);
	mMaterials["castle2"] = std::move(castle2);
}

void TreeBillboardsApp::BuildRenderItems()
{


	////Test
	//auto boxRitem = std::make_unique<RenderItem>();

	////XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(10.0f, 12.0f, 10.0f) * XMMatrixTranslation(0.0f, 6.0f, 0.0f));
	//XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(1.0f, 6.0f, 1.0f) * XMMatrixTranslation(0.0f, 6.0f, 0.0f));
	//boxRitem->ObjCBIndex = 1;
	//boxRitem->Mat = mMaterials["wirefence"].get();
	//boxRitem->Geo = mGeometries["wedgeGeo"].get();
	//boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->IndexCount = boxRitem->Geo->DrawArgs["wedge"].IndexCount;
	//boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	//boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	//mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	//mAllRitems.push_back(std::move(boxRitem));

	//Grid
    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(0.5f, 0.5f, 2.0f) * XMMatrixTranslation(0.0f, 6.0f, 0.0f));
	gridRitem->ObjCBIndex = 0;
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	//Body
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(10.0f, 12.0f, 10.0f) * XMMatrixTranslation(0.0f, 6.0f, 0.0f));
	boxRitem->ObjCBIndex = 1;
	boxRitem->Mat = mMaterials["castle2"].get();
	boxRitem->Geo = mGeometries["boxGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

	//LeftFront Cylinder
	auto cylRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylRitem->World, XMMatrixScaling(6.0f, 5.0f, 6.0f) * XMMatrixTranslation(-6.0f, 7.5f, -5.0f));
	cylRitem->ObjCBIndex = 2;
	cylRitem->Mat = mMaterials["castle2"].get();
	cylRitem->Geo = mGeometries["cylinderGeo"].get();
	cylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylRitem->IndexCount = cylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	cylRitem->StartIndexLocation = cylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylRitem->BaseVertexLocation = cylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(cylRitem.get());
	mAllRitems.push_back(std::move(cylRitem));

	//RightFront Cylinder
	auto cyl2Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cyl2Ritem->World, XMMatrixScaling(6.0f, 5.0f, 6.0f) * XMMatrixTranslation(6.0f, 7.5f, -5.0f));
	cyl2Ritem->ObjCBIndex = 3;
	cyl2Ritem->Mat = mMaterials["castle2"].get();
	cyl2Ritem->Geo = mGeometries["cylinderGeo"].get();
	cyl2Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cyl2Ritem->IndexCount = cyl2Ritem->Geo->DrawArgs["cylinder"].IndexCount;
	cyl2Ritem->StartIndexLocation = cyl2Ritem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cyl2Ritem->BaseVertexLocation = cyl2Ritem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(cyl2Ritem.get());
	mAllRitems.push_back(std::move(cyl2Ritem));

	//LeftBack Cylinder
	auto cyl3Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cyl3Ritem->World, XMMatrixScaling(6.0f, 5.0f, 6.0f) * XMMatrixTranslation(-6.0f, 7.5f, 5.0f));
	cyl3Ritem->ObjCBIndex = 4;
	cyl3Ritem->Mat = mMaterials["castle2"].get();
	cyl3Ritem->Geo = mGeometries["cylinderGeo"].get();
	cyl3Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cyl3Ritem->IndexCount = cyl3Ritem->Geo->DrawArgs["cylinder"].IndexCount;
	cyl3Ritem->StartIndexLocation = cyl3Ritem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cyl3Ritem->BaseVertexLocation = cyl3Ritem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(cyl3Ritem.get());
	mAllRitems.push_back(std::move(cyl3Ritem));

	//RightBack Cylinder
	auto cyl4Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cyl4Ritem->World, XMMatrixScaling(6.0f, 5.0f, 6.0f) * XMMatrixTranslation(6.0f, 7.5f, 5.0f));
	cyl4Ritem->ObjCBIndex = 5;
	cyl4Ritem->Mat = mMaterials["castle2"].get();
	cyl4Ritem->Geo = mGeometries["cylinderGeo"].get();
	cyl4Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cyl4Ritem->IndexCount = cyl4Ritem->Geo->DrawArgs["cylinder"].IndexCount;
	cyl4Ritem->StartIndexLocation = cyl4Ritem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cyl4Ritem->BaseVertexLocation = cyl4Ritem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(cyl4Ritem.get());
	mAllRitems.push_back(std::move(cyl4Ritem));

	//LeftMost Pyramid Front
	auto pyrRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyrRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-3.0f, 12.5f, -4.5f));
	pyrRitem->ObjCBIndex = 6;
	pyrRitem->Mat = mMaterials["wirefence"].get();
	pyrRitem->Geo = mGeometries["pyramidGeo"].get();
	pyrRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyrRitem->IndexCount = pyrRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyrRitem->StartIndexLocation = pyrRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyrRitem->BaseVertexLocation = pyrRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyrRitem.get());
	mAllRitems.push_back(std::move(pyrRitem));

	//SecondLeft Pyramid Front
	auto pyr2Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr2Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-1.5f, 12.5f, -4.5f));
	pyr2Ritem->ObjCBIndex = 7;
	pyr2Ritem->Mat = mMaterials["wirefence"].get();
	pyr2Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr2Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr2Ritem->IndexCount = pyr2Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr2Ritem->StartIndexLocation = pyr2Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr2Ritem->BaseVertexLocation = pyr2Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr2Ritem.get());
	mAllRitems.push_back(std::move(pyr2Ritem));

	//Center Pyramid Front
	auto pyr3Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr3Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 12.5f, -4.5f));
	pyr3Ritem->ObjCBIndex = 8;
	pyr3Ritem->Mat = mMaterials["wirefence"].get();
	pyr3Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr3Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr3Ritem->IndexCount = pyr3Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr3Ritem->StartIndexLocation = pyr3Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr3Ritem->BaseVertexLocation = pyr3Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr3Ritem.get());
	mAllRitems.push_back(std::move(pyr3Ritem));

	//SecondRight Pyramid Front
	auto pyr4Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr4Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(1.5f, 12.5f, -4.5f));
	pyr4Ritem->ObjCBIndex = 9;
	pyr4Ritem->Mat = mMaterials["wirefence"].get();
	pyr4Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr4Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr4Ritem->IndexCount = pyr4Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr4Ritem->StartIndexLocation = pyr4Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr4Ritem->BaseVertexLocation = pyr4Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr4Ritem.get());
	mAllRitems.push_back(std::move(pyr4Ritem));

	//RightMost Pyramid Front
	auto pyr5Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr5Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(3.0f, 12.5f, -4.5f));
	pyr5Ritem->ObjCBIndex = 10;
	pyr5Ritem->Mat = mMaterials["wirefence"].get();
	pyr5Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr5Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr5Ritem->IndexCount = pyr5Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr5Ritem->StartIndexLocation = pyr5Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr5Ritem->BaseVertexLocation = pyr5Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr5Ritem.get());
	mAllRitems.push_back(std::move(pyr5Ritem));

	//Front Pyramid Left
	auto pyr6Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr6Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-4.5f, 12.5f, -1.5f));
	pyr6Ritem->ObjCBIndex = 11;
	pyr6Ritem->Mat = mMaterials["wirefence"].get();
	pyr6Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr6Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr6Ritem->IndexCount = pyr6Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr6Ritem->StartIndexLocation = pyr6Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr6Ritem->BaseVertexLocation = pyr6Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr6Ritem.get());
	mAllRitems.push_back(std::move(pyr6Ritem));

	//Mid Pyramid Left
	auto pyr7Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr7Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-4.5f, 12.5f, 0.0f));
	pyr7Ritem->ObjCBIndex = 12;
	pyr7Ritem->Mat = mMaterials["wirefence"].get();
	pyr7Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr7Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr7Ritem->IndexCount = pyr7Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr7Ritem->StartIndexLocation = pyr7Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr7Ritem->BaseVertexLocation = pyr7Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr7Ritem.get());
	mAllRitems.push_back(std::move(pyr7Ritem));

	//Back Pyramid Left
	auto pyr8Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr8Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-4.5f, 12.5f, +1.5f));
	pyr8Ritem->ObjCBIndex = 13;
	pyr8Ritem->Mat = mMaterials["wirefence"].get();
	pyr8Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr8Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr8Ritem->IndexCount = pyr8Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr8Ritem->StartIndexLocation = pyr8Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr8Ritem->BaseVertexLocation = pyr8Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr8Ritem.get());
	mAllRitems.push_back(std::move(pyr8Ritem));

	//LeftMost Pyramid Back
	auto pyr9Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr9Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-3.0f, 12.5f, 4.5f));
	pyr9Ritem->ObjCBIndex = 14;
	pyr9Ritem->Mat = mMaterials["wirefence"].get();
	pyr9Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr9Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr9Ritem->IndexCount = pyr9Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr9Ritem->StartIndexLocation = pyr9Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr9Ritem->BaseVertexLocation = pyr9Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr9Ritem.get());
	mAllRitems.push_back(std::move(pyr9Ritem));

	//SecondLeft Pyramid Back
	auto pyr10Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr10Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-1.5f, 12.5f, 4.5f));
	pyr10Ritem->ObjCBIndex = 15;
	pyr10Ritem->Mat = mMaterials["wirefence"].get();
	pyr10Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr10Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr10Ritem->IndexCount = pyr10Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr10Ritem->StartIndexLocation = pyr10Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr10Ritem->BaseVertexLocation = pyr10Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr10Ritem.get());
	mAllRitems.push_back(std::move(pyr10Ritem));

	//Mid Pyramid Back
	auto pyr11Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr11Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 12.5f, 4.5f));
	pyr11Ritem->ObjCBIndex = 16;
	pyr11Ritem->Mat = mMaterials["wirefence"].get();
	pyr11Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr11Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr11Ritem->IndexCount = pyr11Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr11Ritem->StartIndexLocation = pyr11Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr11Ritem->BaseVertexLocation = pyr11Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr11Ritem.get());
	mAllRitems.push_back(std::move(pyr11Ritem));

	//SecondRight Pyramid Back
	auto pyr12Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr12Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(1.5f, 12.5f, 4.5f));
	pyr12Ritem->ObjCBIndex = 17;
	pyr12Ritem->Mat = mMaterials["wirefence"].get();
	pyr12Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr12Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr12Ritem->IndexCount = pyr12Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr12Ritem->StartIndexLocation = pyr12Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr12Ritem->BaseVertexLocation = pyr12Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr12Ritem.get());
	mAllRitems.push_back(std::move(pyr12Ritem));

	//RightMost Pyramid Back
	auto pyr13Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr13Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(3.0f, 12.5f, 4.5f));
	pyr13Ritem->ObjCBIndex = 18;
	pyr13Ritem->Mat = mMaterials["wirefence"].get();
	pyr13Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr13Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr13Ritem->IndexCount = pyr13Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr13Ritem->StartIndexLocation = pyr13Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr13Ritem->BaseVertexLocation = pyr13Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr13Ritem.get());
	mAllRitems.push_back(std::move(pyr13Ritem));

	//Front Pyramid Right
	auto pyr14Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr14Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(4.5f, 12.5f, -1.5f));
	pyr14Ritem->ObjCBIndex = 19;
	pyr14Ritem->Mat = mMaterials["wirefence"].get();
	pyr14Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr14Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr14Ritem->IndexCount = pyr14Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr14Ritem->StartIndexLocation = pyr14Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr14Ritem->BaseVertexLocation = pyr14Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr14Ritem.get());
	mAllRitems.push_back(std::move(pyr14Ritem));

	//Mid Pyramid Right
	auto pyr15Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr15Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(4.5f, 12.5f, 0.0f));
	pyr15Ritem->ObjCBIndex = 20;
	pyr15Ritem->Mat = mMaterials["wirefence"].get();
	pyr15Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr15Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr15Ritem->IndexCount = pyr15Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr15Ritem->StartIndexLocation = pyr15Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr15Ritem->BaseVertexLocation = pyr15Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr15Ritem.get());
	mAllRitems.push_back(std::move(pyr15Ritem));

	//Back Pyramid Right
	auto pyr16Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyr16Ritem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(4.5f, 12.5f, 1.5f));
	pyr16Ritem->ObjCBIndex = 21;
	pyr16Ritem->Mat = mMaterials["wirefence"].get();
	pyr16Ritem->Geo = mGeometries["pyramidGeo"].get();
	pyr16Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyr16Ritem->IndexCount = pyr16Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyr16Ritem->StartIndexLocation = pyr16Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyr16Ritem->BaseVertexLocation = pyr16Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyr16Ritem.get());
	mAllRitems.push_back(std::move(pyr16Ritem));

	//
	//Wedge left
	auto wedgeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wedgeRitem->World, XMMatrixScaling(0.5f, 5.0f, 6.0f) * XMMatrixTranslation(-3.0f, 2.5f, -8.0f));
	wedgeRitem->ObjCBIndex = 22;
	wedgeRitem->Mat = mMaterials["wirefence"].get();
	wedgeRitem->Geo = mGeometries["wedgeGeo"].get();
	wedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wedgeRitem->IndexCount = wedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
	wedgeRitem->StartIndexLocation = wedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	wedgeRitem->BaseVertexLocation = wedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wedgeRitem.get());
	mAllRitems.push_back(std::move(wedgeRitem));

	//Wedge Right
	auto wedge1Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wedge1Ritem->World, XMMatrixScaling(0.5f, 5.0f, 6.0f) * XMMatrixTranslation(3.0f, 2.5f, -8.0f));
	wedge1Ritem->ObjCBIndex = 23;
	wedge1Ritem->Mat = mMaterials["wirefence"].get();
	wedge1Ritem->Geo = mGeometries["wedgeGeo"].get();
	wedge1Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wedge1Ritem->IndexCount = wedge1Ritem->Geo->DrawArgs["wedge"].IndexCount;
	wedge1Ritem->StartIndexLocation = wedge1Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
	wedge1Ritem->BaseVertexLocation = wedge1Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wedge1Ritem.get());
	mAllRitems.push_back(std::move(wedge1Ritem));

	//Bridge box
	auto bridgeBoxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&bridgeBoxRitem->World, XMMatrixScaling(6.0f, 0.5f, 6.0f) * XMMatrixTranslation(0.0f, 0.0f, -8.0f));
	bridgeBoxRitem->ObjCBIndex = 24;
	bridgeBoxRitem->Mat = mMaterials["castle"].get();
	bridgeBoxRitem->Geo = mGeometries["boxGeo"].get();
	bridgeBoxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	bridgeBoxRitem->IndexCount = bridgeBoxRitem->Geo->DrawArgs["box"].IndexCount;
	bridgeBoxRitem->StartIndexLocation = bridgeBoxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	bridgeBoxRitem->BaseVertexLocation = bridgeBoxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(bridgeBoxRitem.get());
	mAllRitems.push_back(std::move(bridgeBoxRitem));

	//Diamond
	auto diamondRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(1.0f, 1.0f, 0.5f) * XMMatrixTranslation(0.0f, 11.0f, -5.0f));
	diamondRitem->ObjCBIndex = 25;
	diamondRitem->Mat = mMaterials["water"].get();
	diamondRitem->Geo = mGeometries["diamondGeo"].get();
	diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(diamondRitem.get());
	mAllRitems.push_back(std::move(diamondRitem));

	//Water
   auto wavesRitem = std::make_unique<RenderItem>();
   wavesRitem->World = MathHelper::Identity4x4();
   XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
   wavesRitem->ObjCBIndex = 26;
   wavesRitem->Mat = mMaterials["water"].get();
   wavesRitem->Geo = mGeometries["waterGeo"].get();
   wavesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
   wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
   wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
   mWavesRitem = wavesRitem.get();
   mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());
   mAllRitems.push_back(std::move(wavesRitem));

   
	//UnderGround
   auto gridURitem = std::make_unique<RenderItem>();
   gridURitem->World = MathHelper::Identity4x4();
   XMStoreFloat4x4(&gridURitem->TexTransform, XMMatrixScaling(0.5f, 0.5f, 2.0f)* XMMatrixTranslation(0.0f, 6.0f, 0.0f));
   gridURitem->ObjCBIndex = 28;
   gridURitem->Mat = mMaterials["dirt"].get();
   gridURitem->Geo = mGeometries["landUGeo"].get();
   gridURitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   gridURitem->IndexCount = gridURitem->Geo->DrawArgs["grid"].IndexCount;
   gridURitem->StartIndexLocation = gridURitem->Geo->DrawArgs["grid"].StartIndexLocation;
   gridURitem->BaseVertexLocation = gridURitem->Geo->DrawArgs["grid"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(gridURitem.get());
   mAllRitems.push_back(std::move(gridURitem));

   //FrontLeftWindow
   auto windowFLitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&windowFLitem->World, XMMatrixScaling(2.5f, 2.0f, 0.5f)* XMMatrixTranslation(-2.0f, 9.0f,-4.8f));
   windowFLitem->ObjCBIndex = 29;
   windowFLitem->Mat = mMaterials["window"].get();
   windowFLitem->Geo = mGeometries["boxGeo"].get();
   windowFLitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   windowFLitem->IndexCount = windowFLitem->Geo->DrawArgs["box"].IndexCount;
   windowFLitem->StartIndexLocation = windowFLitem->Geo->DrawArgs["box"].StartIndexLocation;
   windowFLitem->BaseVertexLocation = windowFLitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(windowFLitem.get());
   mAllRitems.push_back(std::move(windowFLitem));

   //FrontRightWindow
   auto windowFRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&windowFRitem->World, XMMatrixScaling(2.5f, 2.0f, 0.5f)* XMMatrixTranslation(2.0f, 9.0f, -4.8f));
   windowFRitem->ObjCBIndex = 30;
   windowFRitem->Mat = mMaterials["window"].get();
   windowFRitem->Geo = mGeometries["boxGeo"].get();
   windowFRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   windowFRitem->IndexCount = windowFRitem->Geo->DrawArgs["box"].IndexCount;
   windowFRitem->StartIndexLocation = windowFRitem->Geo->DrawArgs["box"].StartIndexLocation;
   windowFRitem->BaseVertexLocation = windowFRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(windowFRitem.get());
   mAllRitems.push_back(std::move(windowFRitem));

   //Door
   auto doorRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&doorRitem->World, XMMatrixScaling(5.5f, 5.0f, 0.5f)* XMMatrixTranslation(0.0f, 2.5f, -4.8f));
   doorRitem->ObjCBIndex = 31;
   doorRitem->Mat = mMaterials["window"].get();
   doorRitem->Geo = mGeometries["boxGeo"].get();
   doorRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   doorRitem->IndexCount = doorRitem->Geo->DrawArgs["box"].IndexCount;
   doorRitem->StartIndexLocation = doorRitem->Geo->DrawArgs["box"].StartIndexLocation;
   doorRitem->BaseVertexLocation = doorRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(doorRitem.get());
   mAllRitems.push_back(std::move(doorRitem));

   //DirtRoad
   auto dirtRoadRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&dirtRoadRitem->World, XMMatrixScaling(6.0f, 0.3f, 35.0f)* XMMatrixTranslation(0.0f, 0.0f, -22.5f));
   dirtRoadRitem->ObjCBIndex = 32;
   dirtRoadRitem->Mat = mMaterials["dirt"].get();
   dirtRoadRitem->Geo = mGeometries["boxGeo"].get();
   dirtRoadRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   dirtRoadRitem->IndexCount = dirtRoadRitem->Geo->DrawArgs["box"].IndexCount;
   dirtRoadRitem->StartIndexLocation = dirtRoadRitem->Geo->DrawArgs["box"].StartIndexLocation;
   dirtRoadRitem->BaseVertexLocation = dirtRoadRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(dirtRoadRitem.get());
   mAllRitems.push_back(std::move(dirtRoadRitem));

   //LeftFront Cone
   auto cone1Ritem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cone1Ritem->World, XMMatrixScaling(5.0f, 1.0f, 5.0f)* XMMatrixTranslation(-6.0f, 16.5f, -5.0f));
   cone1Ritem->ObjCBIndex = 33;
   cone1Ritem->Mat = mMaterials["wirefence"].get();
   cone1Ritem->Geo = mGeometries["coneGeo"].get();
   cone1Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cone1Ritem->IndexCount = cone1Ritem->Geo->DrawArgs["cone"].IndexCount;
   cone1Ritem->StartIndexLocation = cone1Ritem->Geo->DrawArgs["cone"].StartIndexLocation;
   cone1Ritem->BaseVertexLocation = cone1Ritem->Geo->DrawArgs["cone"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cone1Ritem.get());
   mAllRitems.push_back(std::move(cone1Ritem));

   //RightFront Cone
   auto cone2Ritem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cone2Ritem->World, XMMatrixScaling(5.0f, 1.0f, 5.0f)* XMMatrixTranslation(6.0f, 16.5f, -5.0f));
   cone2Ritem->ObjCBIndex = 34;
   cone2Ritem->Mat = mMaterials["wirefence"].get();
   cone2Ritem->Geo = mGeometries["coneGeo"].get();
   cone2Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cone2Ritem->IndexCount = cone2Ritem->Geo->DrawArgs["cone"].IndexCount;
   cone2Ritem->StartIndexLocation = cone2Ritem->Geo->DrawArgs["cone"].StartIndexLocation;
   cone2Ritem->BaseVertexLocation = cone2Ritem->Geo->DrawArgs["cone"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cone2Ritem.get());
   mAllRitems.push_back(std::move(cone2Ritem));

   //LeftBack Cone
   auto cone3Ritem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cone3Ritem->World, XMMatrixScaling(5.0f, 1.0f, 5.0f)* XMMatrixTranslation(-6.0f, 16.5f, 5.0f));
   cone3Ritem->ObjCBIndex = 35;
   cone3Ritem->Mat = mMaterials["wirefence"].get();
   cone3Ritem->Geo = mGeometries["coneGeo"].get();
   cone3Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cone3Ritem->IndexCount = cone3Ritem->Geo->DrawArgs["cone"].IndexCount;
   cone3Ritem->StartIndexLocation = cone3Ritem->Geo->DrawArgs["cone"].StartIndexLocation;
   cone3Ritem->BaseVertexLocation = cone3Ritem->Geo->DrawArgs["cone"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cone3Ritem.get());
   mAllRitems.push_back(std::move(cone3Ritem));

   //RightBack Cone
   auto cone4Ritem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cone4Ritem->World, XMMatrixScaling(5.0f, 1.0f, 5.0f)* XMMatrixTranslation(6.0f, 16.5f, 5.0f));
   cone4Ritem->ObjCBIndex = 36;
   cone4Ritem->Mat = mMaterials["wirefence"].get();
   cone4Ritem->Geo = mGeometries["coneGeo"].get();
   cone4Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cone4Ritem->IndexCount = cone4Ritem->Geo->DrawArgs["cone"].IndexCount;
   cone4Ritem->StartIndexLocation = cone4Ritem->Geo->DrawArgs["cone"].StartIndexLocation;
   cone4Ritem->BaseVertexLocation = cone4Ritem->Geo->DrawArgs["cone"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cone4Ritem.get());
   mAllRitems.push_back(std::move(cone4Ritem));

   //Bridge Big box
   auto bridgeBigRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&bridgeBigRitem->World, XMMatrixScaling(10.0f, 0.5f, 30.0f)* XMMatrixTranslation(0.0f, 0.0f, -49.0f));
   bridgeBigRitem->ObjCBIndex = 37;
   bridgeBigRitem->Mat = mMaterials["castle"].get();
   bridgeBigRitem->Geo = mGeometries["boxGeo"].get();
   bridgeBigRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   bridgeBigRitem->IndexCount = bridgeBigRitem->Geo->DrawArgs["box"].IndexCount;
   bridgeBigRitem->StartIndexLocation = bridgeBigRitem->Geo->DrawArgs["box"].StartIndexLocation;
   bridgeBigRitem->BaseVertexLocation = bridgeBigRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(bridgeBigRitem.get());
   mAllRitems.push_back(std::move(bridgeBigRitem));

   //LeftFrontGate Cylinder
   auto cylGateLRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cylGateLRitem->World, XMMatrixScaling(6.0f, 4.0f, 6.0f)* XMMatrixTranslation(-6.0f, 6.0f, -37.0f));
   cylGateLRitem->ObjCBIndex = 38;
   cylGateLRitem->Mat = mMaterials["castle"].get();
   cylGateLRitem->Geo = mGeometries["cylinderGeo"].get();
   cylGateLRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cylGateLRitem->IndexCount = cylGateLRitem->Geo->DrawArgs["cylinder"].IndexCount;
   cylGateLRitem->StartIndexLocation = cylGateLRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
   cylGateLRitem->BaseVertexLocation = cylGateLRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cylGateLRitem.get());
   mAllRitems.push_back(std::move(cylGateLRitem));

   //RightFrontGate Cylinder
   auto cylGateRRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cylGateRRitem->World, XMMatrixScaling(6.0f, 4.0f, 6.0f)* XMMatrixTranslation(6.0f, 6.0f, -37.0f));
   cylGateRRitem->ObjCBIndex = 39;
   cylGateRRitem->Mat = mMaterials["castle"].get();
   cylGateRRitem->Geo = mGeometries["cylinderGeo"].get();
   cylGateRRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cylGateRRitem->IndexCount = cylGateRRitem->Geo->DrawArgs["cylinder"].IndexCount;
   cylGateRRitem->StartIndexLocation = cylGateRRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
   cylGateRRitem->BaseVertexLocation = cylGateRRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cylGateRRitem.get());
   mAllRitems.push_back(std::move(cylGateRRitem));

   //LeftFrontGateW Cylinder
   auto cylGateWLRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cylGateWLRitem->World, XMMatrixScaling(6.0f, 4.0f, 6.0f)* XMMatrixTranslation(-18.0f, 6.0f, -37.0f));
   cylGateWLRitem->ObjCBIndex = 40;
   cylGateWLRitem->Mat = mMaterials["castle"].get();
   cylGateWLRitem->Geo = mGeometries["cylinderGeo"].get();
   cylGateWLRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cylGateWLRitem->IndexCount = cylGateWLRitem->Geo->DrawArgs["cylinder"].IndexCount;
   cylGateWLRitem->StartIndexLocation = cylGateWLRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
   cylGateWLRitem->BaseVertexLocation = cylGateWLRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cylGateWLRitem.get());
   mAllRitems.push_back(std::move(cylGateWLRitem));

   //RightFrontGateW Cylinder
   auto cylGateRWRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cylGateRWRitem->World, XMMatrixScaling(6.0f, 4.0f, 6.0f)* XMMatrixTranslation(18.0f, 6.0f, -37.0f));
   cylGateRWRitem->ObjCBIndex = 41;
   cylGateRWRitem->Mat = mMaterials["castle"].get();
   cylGateRWRitem->Geo = mGeometries["cylinderGeo"].get();
   cylGateRWRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cylGateRWRitem->IndexCount = cylGateRWRitem->Geo->DrawArgs["cylinder"].IndexCount;
   cylGateRWRitem->StartIndexLocation = cylGateRWRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
   cylGateRWRitem->BaseVertexLocation = cylGateRWRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cylGateRWRitem.get());
   mAllRitems.push_back(std::move(cylGateRWRitem));

   //LeftBackGateW Cylinder
   auto cylGateWLBRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cylGateWLBRitem->World, XMMatrixScaling(6.0f, 4.0f, 6.0f)* XMMatrixTranslation(-18.0f, 6.0f, 20.0f));
   cylGateWLBRitem->ObjCBIndex = 42;
   cylGateWLBRitem->Mat = mMaterials["castle"].get();
   cylGateWLBRitem->Geo = mGeometries["cylinderGeo"].get();
   cylGateWLBRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cylGateWLBRitem->IndexCount = cylGateWLBRitem->Geo->DrawArgs["cylinder"].IndexCount;
   cylGateWLBRitem->StartIndexLocation = cylGateWLBRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
   cylGateWLBRitem->BaseVertexLocation = cylGateWLBRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cylGateWLBRitem.get());
   mAllRitems.push_back(std::move(cylGateWLBRitem));

   //RightBackGateW Cylinder
   auto cylGateRWBRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&cylGateRWBRitem->World, XMMatrixScaling(6.0f, 4.0f, 6.0f)* XMMatrixTranslation(18.0f, 6.0f, 20.0f));
   cylGateRWBRitem->ObjCBIndex = 43;
   cylGateRWBRitem->Mat = mMaterials["castle"].get();
   cylGateRWBRitem->Geo = mGeometries["cylinderGeo"].get();
   cylGateRWBRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   cylGateRWBRitem->IndexCount = cylGateRWBRitem->Geo->DrawArgs["cylinder"].IndexCount;
   cylGateRWBRitem->StartIndexLocation = cylGateRWBRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
   cylGateRWBRitem->BaseVertexLocation = cylGateRWBRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(cylGateRWBRitem.get());
   mAllRitems.push_back(std::move(cylGateRWBRitem));

   //Front Uper Wall
   auto outerWallFrontTopRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&outerWallFrontTopRitem->World, XMMatrixScaling(10.0f, 4.0f, 3.0f)* XMMatrixTranslation(0.0f, 8.0f, -37.0f));
   outerWallFrontTopRitem->ObjCBIndex = 44;
   outerWallFrontTopRitem->Mat = mMaterials["castle"].get();
   outerWallFrontTopRitem->Geo = mGeometries["boxGeo"].get();
   outerWallFrontTopRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   outerWallFrontTopRitem->IndexCount = outerWallFrontTopRitem->Geo->DrawArgs["box"].IndexCount;
   outerWallFrontTopRitem->StartIndexLocation = outerWallFrontTopRitem->Geo->DrawArgs["box"].StartIndexLocation;
   outerWallFrontTopRitem->BaseVertexLocation = outerWallFrontTopRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(outerWallFrontTopRitem.get());
   mAllRitems.push_back(std::move(outerWallFrontTopRitem));

   //OuterFrontRightWall
   auto outerWallFrontBRRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&outerWallFrontBRRitem->World, XMMatrixScaling(15.0f, 10.0f, 3.0f)* XMMatrixTranslation(10.0f, 5.0f, -37.0f));
   outerWallFrontBRRitem->ObjCBIndex = 45;
   outerWallFrontBRRitem->Mat = mMaterials["castle"].get();
   outerWallFrontBRRitem->Geo = mGeometries["boxGeo"].get();
   outerWallFrontBRRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   outerWallFrontBRRitem->IndexCount = outerWallFrontBRRitem->Geo->DrawArgs["box"].IndexCount;
   outerWallFrontBRRitem->StartIndexLocation = outerWallFrontBRRitem->Geo->DrawArgs["box"].StartIndexLocation;
   outerWallFrontBRRitem->BaseVertexLocation = outerWallFrontBRRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(outerWallFrontBRRitem.get());
   mAllRitems.push_back(std::move(outerWallFrontBRRitem));

   //OuterFrontLeftWall
   auto outerWallFrontBLRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&outerWallFrontBLRitem->World, XMMatrixScaling(15.0f, 10.0f, 3.0f)* XMMatrixTranslation(-10.0f, 5.0f, -37.0f));
   outerWallFrontBLRitem->ObjCBIndex = 46;
   outerWallFrontBLRitem->Mat = mMaterials["castle"].get();
   outerWallFrontBLRitem->Geo = mGeometries["boxGeo"].get();
   outerWallFrontBLRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   outerWallFrontBLRitem->IndexCount = outerWallFrontBLRitem->Geo->DrawArgs["box"].IndexCount;
   outerWallFrontBLRitem->StartIndexLocation = outerWallFrontBLRitem->Geo->DrawArgs["box"].StartIndexLocation;
   outerWallFrontBLRitem->BaseVertexLocation = outerWallFrontBLRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(outerWallFrontBLRitem.get());
   mAllRitems.push_back(std::move(outerWallFrontBLRitem));

   //OuterBackWall
   auto outerWallBackRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&outerWallBackRitem->World, XMMatrixScaling(36.0f, 10.0f, 3.0f)* XMMatrixTranslation(0.0f, 5.0f, 20.0f));
   outerWallBackRitem->ObjCBIndex = 47;
   outerWallBackRitem->Mat = mMaterials["castle"].get();
   outerWallBackRitem->Geo = mGeometries["boxGeo"].get();
   outerWallBackRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   outerWallBackRitem->IndexCount = outerWallBackRitem->Geo->DrawArgs["box"].IndexCount;
   outerWallBackRitem->StartIndexLocation = outerWallBackRitem->Geo->DrawArgs["box"].StartIndexLocation;
   outerWallBackRitem->BaseVertexLocation = outerWallBackRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(outerWallBackRitem.get());
   mAllRitems.push_back(std::move(outerWallBackRitem));

   //OuterRightWall
   auto outerWallRightRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&outerWallRightRitem->World, XMMatrixScaling(3.0f, 10.0f, 57.0f)* XMMatrixTranslation(18.0f, 5.0f, -10.0f));
   outerWallRightRitem->ObjCBIndex = 48;
   outerWallRightRitem->Mat = mMaterials["castle"].get();
   outerWallRightRitem->Geo = mGeometries["boxGeo"].get();
   outerWallRightRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   outerWallRightRitem->IndexCount = outerWallRightRitem->Geo->DrawArgs["box"].IndexCount;
   outerWallRightRitem->StartIndexLocation = outerWallRightRitem->Geo->DrawArgs["box"].StartIndexLocation;
   outerWallRightRitem->BaseVertexLocation = outerWallRightRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(outerWallRightRitem.get());
   mAllRitems.push_back(std::move(outerWallRightRitem));

   //OuterLeftWall
   auto outerWallLeftRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&outerWallLeftRitem->World, XMMatrixScaling(3.0f, 10.0f, 57.0f)* XMMatrixTranslation(-18.0f, 5.0f, -10.0f));
   outerWallLeftRitem->ObjCBIndex = 49;
   outerWallLeftRitem->Mat = mMaterials["castle"].get();
   outerWallLeftRitem->Geo = mGeometries["boxGeo"].get();
   outerWallLeftRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   outerWallLeftRitem->IndexCount = outerWallLeftRitem->Geo->DrawArgs["box"].IndexCount;
   outerWallLeftRitem->StartIndexLocation = outerWallLeftRitem->Geo->DrawArgs["box"].StartIndexLocation;
   outerWallLeftRitem->BaseVertexLocation = outerWallLeftRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(outerWallLeftRitem.get());
   mAllRitems.push_back(std::move(outerWallLeftRitem));

   //Bridge Right Big box
   auto bridgeBigRightRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&bridgeBigRightRitem->World, XMMatrixScaling(1.0f, 2.0f, 30.0f)* XMMatrixTranslation(4.5f, 1.0f, -49.0f));
   bridgeBigRightRitem->ObjCBIndex = 50;
   bridgeBigRightRitem->Mat = mMaterials["castle"].get();
   bridgeBigRightRitem->Geo = mGeometries["boxGeo"].get();
   bridgeBigRightRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   bridgeBigRightRitem->IndexCount = bridgeBigRightRitem->Geo->DrawArgs["box"].IndexCount;
   bridgeBigRightRitem->StartIndexLocation = bridgeBigRightRitem->Geo->DrawArgs["box"].StartIndexLocation;
   bridgeBigRightRitem->BaseVertexLocation = bridgeBigRightRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(bridgeBigRightRitem.get());
   mAllRitems.push_back(std::move(bridgeBigRightRitem));

   //Bridge Left Big box
   auto bridgeBigLeftRitem = std::make_unique<RenderItem>();
   XMStoreFloat4x4(&bridgeBigLeftRitem->World, XMMatrixScaling(1.0f, 2.0f, 30.0f)* XMMatrixTranslation(-4.5f, 1.0f, -49.0f));
   bridgeBigLeftRitem->ObjCBIndex = 51;
   bridgeBigLeftRitem->Mat = mMaterials["castle"].get();
   bridgeBigLeftRitem->Geo = mGeometries["boxGeo"].get();
   bridgeBigLeftRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   bridgeBigLeftRitem->IndexCount = bridgeBigLeftRitem->Geo->DrawArgs["box"].IndexCount;
   bridgeBigLeftRitem->StartIndexLocation = bridgeBigLeftRitem->Geo->DrawArgs["box"].StartIndexLocation;
   bridgeBigLeftRitem->BaseVertexLocation = bridgeBigLeftRitem->Geo->DrawArgs["box"].BaseVertexLocation;
   mRitemLayer[(int)RenderLayer::Opaque].push_back(bridgeBigLeftRitem.get());
   mAllRitems.push_back(std::move(bridgeBigLeftRitem));


	//
	//TransparentPrism
	//auto boxRitem = std::make_unique<RenderItem>();
	//XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
	//boxRitem->ObjCBIndex = 2;
	//boxRitem->Mat = mMaterials["wirefence"].get();
	//boxRitem->Geo = mGeometries["boxGeo"].get();
	//boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	//boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	//boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	//mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem.get());
	
	//Trees
	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = 52;
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	//step2
	treeSpritesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());
	mAllRitems.push_back(std::move(treeSpritesRitem));

	//mAllRitems.push_back(std::move(boxRitem));

}

void TreeBillboardsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		//step3
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TreeBillboardsApp::GetStaticSamplers()
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

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}

float TreeBillboardsApp::GetHillsHeight(float x, float z)const
{
    return 0.3f*(z*sinf(0.1f*x) + x*cosf(0.1f*z));
}

XMFLOAT3 TreeBillboardsApp::GetHillsNormal(float x, float z)const
{
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
        1.0f,
        -0.3f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}
