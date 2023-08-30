#pragma once
#include "Common/D3DApp.h"
#include "UploadBuffer.h"

struct Vertex
{
	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT4 color;
};

struct ObjectConstants
{
	DirectX::XMFLOAT4X4 MVP = MathHelper::Identity4x4();
};

class BoxApp : public D3DApp
{
public:
	BoxApp(HINSTANCE hInstance);
	BoxApp(BoxApp& rhs) = delete;
	BoxApp& operator=(const BoxApp& rhs) = delete;
	~BoxApp();

	virtual bool Initialize() override;
private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildBoxGeometry();
	void BuildPSO();
private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> pRootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pCbvHeap = nullptr;

	std::unique_ptr<UploadBuffer<ObjectConstants>> pObjectCB = nullptr;

	std::unique_ptr<MeshGeometry> pBoxGeo = nullptr;

	Microsoft::WRL::ComPtr<ID3DBlob> pVSBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> pPSBlob = nullptr;

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pPSO = nullptr;

	DirectX::XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = DirectX::XM_PIDIV4;
	float mRadius = 5.0f;

	POINT mLastMousePos;

};