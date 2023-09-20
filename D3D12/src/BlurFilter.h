#pragma once
#include "Common/d3dUtil.h"

class BlurFilter
{
public:
	BlurFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);
	void BuildBlurTex(ID3D12Device* device);
	ID3D12Resource* Resource();

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> blurMap{};
	UINT width, height{};
	DXGI_FORMAT format{};
};