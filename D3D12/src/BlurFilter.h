#pragma once
#include "Common/d3dUtil.h"

class BlurFilter
{
public:
	BlurFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);
	void BuildBlurTex();
	void BuildDescriptorHeap();
	void CreateView();
	void BuildRootSignature();
	void BuildComputeShaderAndPSO();
	void Execute(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* input);
	ID3D12Resource* Resource();

private:
	ID3D12Device* device{};
	Microsoft::WRL::ComPtr<ID3D12Resource> blurMap{};
	UINT width, height{};
	DXGI_FORMAT format{};

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> uavHeap{};
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig{};
	Microsoft::WRL::ComPtr<ID3D12PipelineState> PSO{};
	Microsoft::WRL::ComPtr<ID3DBlob> CS;
};