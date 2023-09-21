#include "BlurFilter.h"

BlurFilter::BlurFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
   : device(device), width(width), height(height), format(format)
{
    BuildDescriptorHeap();
    BuildBlurTex();
    CreateView();
    BuildRootSignature();
    BuildComputeShaderAndPSO();
}

void BlurFilter::BuildBlurTex()
{
    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height,
        1U, 1U, 1U,
        0U, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );

    ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&blurMap)
    ));

    blurMap->SetName(L"BLUR MAP");
}

void BlurFilter::BuildDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc{};
    uavHeapDesc.NumDescriptors = 1;
    uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&uavHeap)));

}

void BlurFilter::CreateView()
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    device->CreateUnorderedAccessView(blurMap.Get(), nullptr, &uavDesc, uavHeap->GetCPUDescriptorHandleForHeapStart());
}

void BlurFilter::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER rootParams[1]{};
    CD3DX12_DESCRIPTOR_RANGE texTable(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
        1, 0U);
    rootParams[0].InitAsDescriptorTable(1, &texTable);

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

void BlurFilter::BuildComputeShaderAndPSO()
{
    ThrowIfFailed(D3DReadFileToBlob(L"Shaders\\ShaderBins\\BlurCS.cso", CS.GetAddressOf()));

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc{};
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(CS.Get());
    computePsoDesc.pRootSignature = rootSig.Get();
    computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&PSO)));
}

void BlurFilter::Execute(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* input)
{
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        input, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE
    ));
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        blurMap.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST
    ));
    cmdList->CopyResource(blurMap.Get(), input);

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        blurMap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    ));

    cmdList->SetPipelineState(PSO.Get());

    cmdList->SetComputeRootSignature(rootSig.Get());

    ID3D12DescriptorHeap* heaps[] = { uavHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRootDescriptorTable(0U, uavHeap->GetGPUDescriptorHandleForHeapStart());

    UINT numGroupsX = (UINT)ceilf(width / 256.0f);
    cmdList->Dispatch(numGroupsX, height, 1);

    D3D12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(blurMap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE), CD3DX12_RESOURCE_BARRIER::Transition(input, D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_COPY_DEST) };
    cmdList->ResourceBarrier(2, barriers);
    cmdList->CopyResource(input, blurMap.Get());

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        blurMap.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON
    ));

    


}

ID3D12Resource* BlurFilter::Resource()
{
    return blurMap.Get();
}
