#include "BoxApp.h"

BoxApp::BoxApp(HINSTANCE hInstance)
    :D3DApp(hInstance)
{
}

BoxApp::~BoxApp()
{
}

bool BoxApp::Initialize()
{
    if (!D3DApp::Initialize())
    {
        return false;
    }

    return true;
}

void BoxApp::OnResize()
{
    D3DApp::OnResize();
}

void BoxApp::Update(const GameTimer& gt)
{
}

void BoxApp::Draw(const GameTimer& gt)
{
    ThrowIfFailed(pCmdListAlloc->Reset());
    ThrowIfFailed(pCommandList->Reset(pCmdListAlloc.Get(), nullptr));

    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
    ));

    pCommandList->RSSetViewports(1, &screenViewport);
    pCommandList->RSSetScissorRects(1, &scissorRect);

    pCommandList->ClearRenderTargetView(
        CurrentBackBufferView(), DirectX::Colors::DimGray, 0, nullptr);
    pCommandList->ClearDepthStencilView(
        DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    pCommandList->OMSetRenderTargets(
        1, &CurrentBackBufferView(), true, &DepthStencilView());

    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
    ));

    ThrowIfFailed(pCommandList->Close());

    ID3D12CommandList* cmdLists[] = { pCommandList.Get() };
    pCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    pSwapChain->Present(0U, 0U);
    currBackBuffer = (currBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();

}

void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
}

void BoxApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.NodeMask = 0;
    cbvHeapDesc.NumDescriptors = 1;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(pDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&pCbvHeap)));
}

void BoxApp::BuildConstantBuffers()
{
    pObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(pDevice.Get(), 1, true);

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = pObjectCB->Resource()->GetGPUVirtualAddress();
    // Offset to the ith object constant buffer in the buffer.
    int boxCBufIndex = 0;
    cbAddress += boxCBufIndex * objCBByteSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    pDevice->CreateConstantBufferView(
        &cbvDesc,
        pCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void BoxApp::BuildRootSignature()
{
    // only one root parameter needed for the mvp matrix
    CD3DX12_ROOT_PARAMETER rootParams[1]{};
    CD3DX12_DESCRIPTOR_RANGE cbvTable(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0U);
    rootParams[0].InitAsDescriptorTable(1, &cbvTable);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc((UINT)std::size(rootParams), rootParams,
        0U, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> rootSigBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
    auto hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        rootSigBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (errorBlob)
    {
        auto outputString = std::string("Error regarding Root Signature Serialization :") + (char*)errorBlob->GetBufferPointer();
        OutputDebugStringA(outputString.c_str());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(pDevice->CreateRootSignature(0, rootSigBlob->GetBufferPointer(),
        rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSignature)));
}

void BoxApp::BuildShadersAndInputLayout()
{
    ThrowIfFailed(D3DReadFileToBlob(L"Shaders\\ColorVS.hlsl", pVSBlob.GetAddressOf()));
    ThrowIfFailed(D3DReadFileToBlob(L"Shaders\\ColorPS.hlsl", pPSBlob.GetAddressOf()));

    inputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void BoxApp::BuildBoxGeometry()
{
    std::array<Vertex, 8> vertices =
    {
        Vertex({ DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::White) }),
        Vertex({ DirectX::XMFLOAT3(-1.0f, +1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Black) }),
        Vertex({ DirectX::XMFLOAT3(+1.0f, +1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Red) }),
        Vertex({ DirectX::XMFLOAT3(+1.0f, -1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Green) }),
        Vertex({ DirectX::XMFLOAT3(-1.0f, -1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Blue) }),
        Vertex({ DirectX::XMFLOAT3(-1.0f, +1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Yellow) }),
        Vertex({ DirectX::XMFLOAT3(+1.0f, +1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Cyan) }),
        Vertex({ DirectX::XMFLOAT3(+1.0f, -1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Magenta) })
    };

    std::array<std::uint16_t, 36> indices =
    {
        // front face
        0, 1, 2,
        0, 2, 3,

        // back face
        4, 6, 5,
        4, 7, 6,

        // left face
        4, 5, 1,
        4, 1, 0,

        // right face
        3, 2, 6,
        3, 6, 7,

        // top face
        1, 5, 6,
        1, 6, 2,

        // bottom face
        4, 0, 3,
        4, 3, 7
    };

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    pBoxGeo = std::make_unique<MeshGeometry>();
    pBoxGeo->Name = "boxGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &pBoxGeo->VertexBufferCPU));
    CopyMemory(pBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &pBoxGeo->IndexBufferCPU));
    CopyMemory(pBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    pBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(pDevice.Get(),
        pCommandList.Get(), vertices.data(), vbByteSize, pBoxGeo->VertexBufferUploader);

    pBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(pDevice.Get(),
        pCommandList.Get(), indices.data(), ibByteSize, pBoxGeo->IndexBufferUploader);

    pBoxGeo->VertexByteStride = sizeof(Vertex);
    pBoxGeo->VertexBufferByteSize = vbByteSize;
    pBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
    pBoxGeo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    pBoxGeo->DrawArgs["box"] = submesh;
}

void BoxApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVSBlob.Get());
}
