#include "BlurFilter.h"

BlurFilter::BlurFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
   : device(device), width(width), height(height), format(format)
{
    BuildDescriptorHeap();
    BuildBlurTex();
    BuildRootSignature();
}

void BlurFilter::BuildBlurTex()
{
    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height,
        1U, 1U, 1U,
        0U, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );

    ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&blurMap)
    ));

    blurMap->SetName(L"BLUR MAP");
}

void BlurFilter::BuildDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc{};
    uavHeapDesc.NumDescriptors = 1;
    uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    ThrowIfFailed(device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&uavHeap)));
}

void BlurFilter::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER rootParams[1]{};
    rootParams->InitAsUnorderedAccessView(0U);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        1, rootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(device->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(rootSig.GetAddressOf())));
}

void BlurFilter::Execute(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* input)
{
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        input, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE
    ));
    cmdList->CopyResource(blurMap.Get(), input);

    


}

ID3D12Resource* BlurFilter::Resource()
{
    return blurMap.Get();
}
