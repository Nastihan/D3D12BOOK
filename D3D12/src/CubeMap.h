#pragma once
#include "Common/d3dUtil.h"

class CubeMap
{
public:
	CubeMap(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
	void BuildResource();
	void BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle, 
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv, CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv);
	auto Rtv(UINT i)
	{
		return cpuRtvs[i];
	}
	auto Dsv()
	{
		return cpuDsv;
	}
	ID3D12Resource* Resource()
	{
		return RT.Get();
	}

private:
	ID3D12Device* device{};
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap{};
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap{};
	Microsoft::WRL::ComPtr<ID3D12Resource> RT{};
	Microsoft::WRL::ComPtr<ID3D12Resource> DS{};
	UINT width, height = 0;
	DXGI_FORMAT rtvFormat;
	DXGI_FORMAT dsvFormat;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDsv{};
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtvs[6]{};
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv{};
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv{};
};