#pragma once
#include "Common/D3DApp.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include "Common/Waves.h"

struct RenderItem
{
	RenderItem() = default;

	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();

	int NumFramesDirty = gNumFrameResources;

	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Count
};

class LandAndWavesApp : public D3DApp
{
public:
	LandAndWavesApp(HINSTANCE hInstance);
	LandAndWavesApp(LandAndWavesApp& rhs) = delete;
	LandAndWavesApp& operator=(const LandAndWavesApp& rhs) = delete;
	~LandAndWavesApp();

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnkeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gf);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);

	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildLandGeometry();
	void BuildWavesGeometryBuffers();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRendeItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& rItems);

	float GetHillsHeight(float x, float z)const;
	DirectX::XMFLOAT3 GetHillsNormal(float x, float z)const;
private:
	std::vector<std::unique_ptr<FrameResource>> frameResources;
	FrameResource* currFrameResource = nullptr;
	int currFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> pRootSignature = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> shaders;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> PSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	RenderItem* wavesRItem = nullptr;

	// all of the render items
	std::vector<std::unique_ptr<RenderItem>> allRItems;
	// render items divided by PSO
	std::vector<RenderItem*> renderItemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

	PassConstants mainPassCB;

	bool isWireframe = false;

	DirectX::XMFLOAT3 eyePos = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 view = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 proj = MathHelper::Identity4x4();

	float theta = 1.5f * DirectX::XM_PI;
	float phi = DirectX::XM_PIDIV4;
	float radius = 40.0f;

	float sunTheta = 1.25 * DirectX::XM_PI;
	float sunPhi = DirectX::XM_PIDIV4;

	POINT lastMousePos;

};