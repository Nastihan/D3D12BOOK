#include "BlurFilter.h"

BlurFilter::BlurFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
   : width(width), height(height), format(format)
{
    BuildBlurTex(device);
}

void BlurFilter::BuildBlurTex(ID3D12Device* device)
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

ID3D12Resource* BlurFilter::Resource()
{
    return blurMap.Get();
}
