#include "CubeMap.h"

CubeMap::CubeMap(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
	:device(device), width(width), height(height), rtvFormat(rtvFormat), dsvFormat(dsvFormat)
{
	BuildResource();
}

void CubeMap::BuildResource()
{
	auto rtvTexDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		rtvFormat, width, height, 1U, 0U, 1U, 0U,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);

	float Color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	CD3DX12_CLEAR_VALUE clearValue(rtvFormat, Color);

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE, &rtvTexDesc, D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clearValue, IID_PPV_ARGS(&RT)
	));


	
	auto dsvTexDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		dsvFormat, width, height, 1U, 0U, 1U, 0U,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	);
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE, &dsvTexDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&CD3DX12_CLEAR_VALUE(dsvFormat, 1.0f, 0.0f), IID_PPV_ARGS(&DS)
	));

}

void CubeMap::BuildDescriptors(D3D12_CPU_DESCRIPTOR_HANDLE cpuRtv, D3D12_CPU_DESCRIPTOR_HANDLE cpuDsv)
{
	this->cpuRtv = cpuRtv;
	this->cpuDsv = cpuDsv;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = dsvFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	device->CreateDepthStencilView(DS.Get(), &dsvDesc, cpuDsv);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = rtvFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2DArray.MipSlice = 0;
	rtvDesc.Texture2DArray.PlaneSlice = 0;
	rtvDesc.Texture2DArray.FirstArraySlice = 0;
	rtvDesc.Texture2DArray.ArraySize = 1;
	device->CreateRenderTargetView(RT.Get(), &rtvDesc, cpuRtv);
}
