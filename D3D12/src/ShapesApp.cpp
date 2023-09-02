#include "ShapesApp.h"

ShapesApp::ShapesApp(HINSTANCE hInstance)
    :D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
}

bool ShapesApp::Initialize()
{
    if (!D3DApp::Initialize())
    {
        return false;
    }

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(pCommandList->Reset(pCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildRenderItems();
    BuildFrameResources();
    BuildDescriptorHeaps();
    BuildConstantBufferViews();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(pCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { pCommandList.Get() };
    pCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}

void ShapesApp::OnResize()
{
    D3DApp::OnResize();

    DirectX::XMMATRIX P = DirectX::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, GetAR(), 1.0f, 1000.0f);
    DirectX::XMStoreFloat4x4(&proj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
    OnkeyboardInput(gt);
    UpdateCamera(gt);

    currFrameResourceIndex = (currFrameResourceIndex + 1) % gNumFrameResources;
    currFrameResource = frameResources[currFrameResourceIndex].get();

    if (pFence->GetCompletedValue() < currFrameResource->fenceVal)
    {
        HANDLE eventHanlde = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(pFence->SetEventOnCompletion(currFrameResource->fenceVal, eventHanlde));
        WaitForSingleObject(eventHanlde, INFINITE);
        CloseHandle(eventHanlde);
    }

    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
    ThrowIfFailed(pCmdListAlloc->Reset());
    ThrowIfFailed(pCommandList->Reset(pCmdListAlloc.Get(), nullptr));

    pCommandList->RSSetViewports(1, &screenViewport);
    pCommandList->RSSetScissorRects(1, &scissorRect);

    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
    ));

    pCommandList->ClearRenderTargetView(
        CurrentBackBufferView(), DirectX::Colors::DimGray, 0, nullptr);
    pCommandList->ClearDepthStencilView(
        DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    pCommandList->OMSetRenderTargets(
        1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { pCbvHeap.Get() };
    pCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    pCommandList->SetGraphicsRootSignature(pRootSignature.Get());
    pCommandList->SetGraphicsRootDescriptorTable(0, pCbvHeap->GetGPUDescriptorHandleForHeapStart());
    pCommandList->IASetIndexBuffer(&pBoxGeo->IndexBufferView());
    pCommandList->IASetVertexBuffers(0, 1, &pBoxGeo->VertexBufferView());
    pCommandList->SetPipelineState(pPSO.Get());
    pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pCommandList->DrawIndexedInstanced(pBoxGeo->DrawArgs["box"].IndexCount, 1, 0, 0, 0);

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

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    lastMousePos.x = x;
    lastMousePos.y = y;

    SetCapture(mainHWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    using namespace DirectX;
    if ((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - lastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - lastMousePos.y));

        // Update angles based on input to orbit camera around box.
        theta += dx;
        phi += dy;

        // Restrict the angle mPhi.
        phi = MathHelper::Clamp(phi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.005 unit in the scene.
        float dx = 0.005f * static_cast<float>(x - lastMousePos.x);
        float dy = 0.005f * static_cast<float>(y - lastMousePos.y);

        // Update the camera radius based on input.
        radius += dx - dy;

        // Restrict the radius.
        radius = MathHelper::Clamp(radius, 3.0f, 15.0f);
    }

    lastMousePos.x = x;
    lastMousePos.y = y;
}

void ShapesApp::OnkeyboardInput(const GameTimer& gt)
{
    if (GetAsyncKeyState('1') & 0x8000)
        isWireframe = true;
    else
        isWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
    eyePos.x = radius * sinf(phi) * cosf(theta);
    eyePos.z = radius * sinf(phi) * sinf(theta);
    eyePos.y = radius * cosf(phi);

    // view matrix
    DirectX::XMVECTOR pos = DirectX::XMVectorSet(eyePos.x, eyePos.y, eyePos.z, 1.0f);
    DirectX::XMVECTOR target = DirectX::XMVectorZero();
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(pos, target, up);
    DirectX::XMStoreFloat4x4(&this->view, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gf)
{
    auto currObjectCB = currFrameResource->ObjectCB.get();
    for (auto& e : allRItems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if (e->numFramesDirty > 0)
        {
            DirectX::XMMATRIX world = XMLoadFloat4x4(&e->World);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

            currObjectCB->CopyData(e->objCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->numFramesDirty--;
        }
    }
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
    DirectX::XMMATRIX view = XMLoadFloat4x4(&this->view);
    DirectX::XMMATRIX proj = XMLoadFloat4x4(&this->proj);

    DirectX::XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    DirectX::XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    DirectX::XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    DirectX::XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    DirectX::XMStoreFloat4x4(&mainPassCB.View, XMMatrixTranspose(view));
    DirectX::XMStoreFloat4x4(&mainPassCB.InvView, XMMatrixTranspose(invView));
    DirectX::XMStoreFloat4x4(&mainPassCB.Proj, XMMatrixTranspose(proj));
    DirectX::XMStoreFloat4x4(&mainPassCB.InvProj, XMMatrixTranspose(invProj));
    DirectX::XMStoreFloat4x4(&mainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    DirectX::XMStoreFloat4x4(&mainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mainPassCB.EyePosW = eyePos;
    mainPassCB.RenderTargetSize = DirectX::XMFLOAT2((float)clientWidth, (float)clientHeight);
    mainPassCB.InvRenderTargetSize = DirectX::XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
    mainPassCB.NearZ = 1.0f;
    mainPassCB.FarZ = 1000.0f;
    mainPassCB.TotalTime = gt.TotalTime();
    mainPassCB.DeltaTime = gt.DeltaTime();

    auto currPassCB = currFrameResource->PassCB.get();
    currPassCB->CopyData(0, mainPassCB);
}

void ShapesApp::BuildDescriptorHeaps()
{
    UINT objCount = (UINT)opaqueRItems.size();

    // Need a CBV descriptor for each object for each frame resource,
    // +1 for the perPass CBV for each frame resource.
    UINT numDescriptors = (objCount + 1) * gNumFrameResources;

    // Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
    passCbvOffset = objCount * gNumFrameResources;

    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = numDescriptors;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(pDevice->CreateDescriptorHeap(&cbvHeapDesc,
        IID_PPV_ARGS(&pCbvHeap)));
}

void ShapesApp::BuildConstantBufferViews()
{
    UINT objCbByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT objCount = (UINT)opaqueRItems.size();

    for (int frameIndex = 0; frameIndex < gNumFrameResources; frameIndex++)
    {
        auto objectCB = frameResources[frameIndex]->ObjectCB->Resource();
        for (int i = 0; i < objCount; i++)
        {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

            cbAddress += i * objCbByteSize;

            int heapIndex = frameIndex*objCount + i;
            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
                pCbvHeap->GetCPUDescriptorHandleForHeapStart(),
                heapIndex,
                cbvSrvUavDescriptorSize
            );

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
            cbvDesc.SizeInBytes = objCbByteSize;
            cbvDesc.BufferLocation = cbAddress;

            pDevice->CreateConstantBufferView(&cbvDesc, handle);
        }
    }

    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

    for (int frameIndex = 0; frameIndex < gNumFrameResources; frameIndex++)
    {
        auto passCb = frameResources[frameIndex]->PassCB->Resource();
        D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCb->GetGPUVirtualAddress();

        // offset to the pass cbv in the descriptor heap
        auto heapIndex = passCbvOffset + frameIndex;
        auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            pCbvHeap->GetCPUDescriptorHandleForHeapStart(),
            heapIndex,
            cbvSrvUavDescriptorSize
        );

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
        cbvDesc.BufferLocation = cbAddress;
        cbvDesc.SizeInBytes = passCBByteSize;

        pDevice->CreateConstantBufferView(
            &cbvDesc, handle
        );
    }
}


void ShapesApp::BuildRootSignature()
{
    // only one root parameter needed for the mvp matrix
    CD3DX12_ROOT_PARAMETER rootParams[2]{};
    CD3DX12_DESCRIPTOR_RANGE cbvTable0(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0U);
    rootParams[0].InitAsDescriptorTable(1, &cbvTable0);
    CD3DX12_DESCRIPTOR_RANGE cbvTable1(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1U);
    rootParams[1].InitAsDescriptorTable(1, &cbvTable1);

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

void ShapesApp::BuildShadersAndInputLayout()
{
    ThrowIfFailed(D3DReadFileToBlob(L"Shaders\\ShaderBins\\standardVS.cso", shaders["standardVS"].GetAddressOf()));
    ThrowIfFailed(D3DReadFileToBlob(L"Shaders\\ShaderBins\\opaquePS.cso", shaders["opaquePS"].GetAddressOf()));


    inputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void ShapesApp::BuildShapeGeometry()
{
}

void ShapesApp::BuildPSOs()
{
}

void ShapesApp::BuildFrameResources()
{
}

void ShapesApp::BuildRenderItems()
{
}

void ShapesApp::DrawRendeItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem>& rItems)
{
}

void ShapesApp::BuildBoxGeometry()
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

void ShapesApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVSBlob.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPSBlob.Get());
    psoDesc.pRootSignature = pRootSignature.Get();
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = backBufferFormat;
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.DSVFormat = depthStencilFormat;

    ThrowIfFailed(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pPSO)));

}
