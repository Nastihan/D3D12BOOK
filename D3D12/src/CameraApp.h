#pragma once
#include "Common/D3DApp.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include "Camera.h"

struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int NumFramesDirty = gNumFrameResources;

	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class CameraApp : public D3DApp
{
public:
	CameraApp(HINSTANCE hInstance);
	CameraApp(CameraApp& rhs) = delete;
	CameraApp& operator=(const CameraApp& rhs) = delete;
	~CameraApp();

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& rItems); \

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6>  GetStaticSamplers();
private:
	Camera cam;

	std::vector<std::unique_ptr<FrameResource>> frameResources;
	FrameResource* currFrameResource = nullptr;
	int currFrameResourceIndex = 0;

	UINT cbvSrvDescriptorSize = 0;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> pRootSignature = nullptr;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
	std::unordered_map<std::string, std::unique_ptr<Material>> materials;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> shaders;


	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> opaquePSO = nullptr;

	// all of the render items
	std::vector<std::unique_ptr<RenderItem>> allRItems;
	// render items divided by PSO
	std::vector<RenderItem*> opaqueRItems;

	PassConstants mainPassCB;

	DirectX::XMFLOAT3 eyePos = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 view = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 proj = MathHelper::Identity4x4();

	float theta = 1.5f * DirectX::XM_PI;
	float phi = DirectX::XM_PIDIV2 - 0.3f;
	float radius = 25.0f;

	POINT lastMousePos;
};