#pragma once
#include "Common/d3dUtil.h"

template <typename T>
class UploadBuffer
{
public:
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
		:isConstantBuffer(isConstantBuffer)
	{
		elementByteSize = sizeof(T);
		if (isConstantBuffer)
		{
			elementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));
		}

		ThrowIfFailed(device->CreateCommittedResource(
			CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			CD3DX12_RESOURCE_DESC::Buffer(elementByteSize * elementCount),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&pUploadBuffer)
		));

		ThrowIfFailed(pUploadBuffer->Map(0, nullptr, reinterpret_cast<void** < (mappedData)));
		// We do not need to unmap until we are done with the resource.  However, we must not write to
		// the resource while it is in use by the GPU (so we must use synchronization techniques).
	}
	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
	~UploadBuffer()
	{
		if (pUploadBuffer != nullptr)
		{
			pUploadBuffer->Unmap(0, nullptr);
		}
		mappedData = nullptr;
	}
	ID3D12Resource* Resource() const
	{
		return pUploadBuffer.Get();
	}
	void CopyData(int elementIndex, const T& data)
	{
		memccpy(&mappedData[elementIndex * elementByteSize], &data, sizeof(T));
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> pUploadBuffer;
	BYTE* mappedData = nullptr;
	UINT elementByteSize = 0;
	bool isConstantBuffer = flase;
};