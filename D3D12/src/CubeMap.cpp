#include "CubeMap.h"

CubeMap::CubeMap(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
	:device(device), width(width), height(height), rtvFormat(rtvFormat), dsvFormat(dsvFormat)
{
	BuildResource();
}

void CubeMap::BuildResource()
{
	auto rtvTexDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		rtvFormat, width, height, 6U, 1U, 1U, 0U,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);

	float Color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	CD3DX12_CLEAR_VALUE clearValue(rtvFormat, Color);

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE, &rtvTexDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		&clearValue, IID_PPV_ARGS(&RT)
	));
	
	auto dsvTexDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		dsvFormat, width, height, 1U, 0U, 1U, 0U,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	);
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE, &dsvTexDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&CD3DX12_CLEAR_VALUE(dsvFormat, 1.0f, 0), IID_PPV_ARGS(&DS)
	));

}

void CubeMap::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv, 
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle,
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv, CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv)
{
	for (size_t i = 0; i < 6; i++)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(cpuRtv, i, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
		this->cpuRtvs[i] = handle;
	}
	this->cpuDsv = dsvHandle;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = dsvFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	device->CreateDepthStencilView(DS.Get(), &dsvDesc, cpuDsv);

	for (size_t i =0; i < 6; i++)
	{
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = rtvFormat;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		rtvDesc.Texture2DArray.MipSlice = 0;
		rtvDesc.Texture2DArray.PlaneSlice = 0;
		rtvDesc.Texture2DArray.FirstArraySlice = (int)i;
		rtvDesc.Texture2DArray.ArraySize = 1;
		device->CreateRenderTargetView(RT.Get(), &rtvDesc, cpuRtvs[i]);
	}

	this->cpuSrv = cpuSrv;
	this->gpuSrv = gpuSrv;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = rtvFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.TextureCube.MipLevels = 1;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;


	device->CreateShaderResourceView(RT.Get(), &srvDesc, cpuSrv);
}


