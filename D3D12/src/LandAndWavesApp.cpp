#include "LandAndWavesApp.h"
#include "Common/GeometryGenerator.h"


LandAndWavesApp::LandAndWavesApp(HINSTANCE hInstance)
    :D3DApp(hInstance)
{
}

LandAndWavesApp::~LandAndWavesApp()
{
}

bool LandAndWavesApp::Initialize()
{
    if (!D3DApp::Initialize())
    {
        return false;
    }

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(pCommandList->Reset(pCmdListAlloc.Get(), nullptr));

    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildLandGeometry();
    BuildWavesGeometryBuffers();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(pCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { pCommandList.Get() };
    pCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}

void LandAndWavesApp::OnResize()
{
    D3DApp::OnResize();

    DirectX::XMMATRIX P = DirectX::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, GetAR(), 1.0f, 1000.0f);
    DirectX::XMStoreFloat4x4(&proj, P);
}

void LandAndWavesApp::Update(const GameTimer& gt)
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
    UpdateWaves(gt);
}

void LandAndWavesApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = currFrameResource->pCmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());
    if (isWireframe)
    {
        ThrowIfFailed(pCommandList->Reset(cmdListAlloc.Get(), PSOs["opaque_wireframe"].Get()));
    }
    else
    {
        ThrowIfFailed(pCommandList->Reset(cmdListAlloc.Get(), PSOs["opaque"].Get()));
    }
    pCommandList->RSSetViewports(1, &screenViewport);
    pCommandList->RSSetScissorRects(1, &scissorRect);

    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
    ));

    pCommandList->ClearRenderTargetView(
        CurrentBackBufferView(), DirectX::Colors::AliceBlue, 0, nullptr);
    pCommandList->ClearDepthStencilView(
        DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    pCommandList->OMSetRenderTargets(
        1, &CurrentBackBufferView(), true, &DepthStencilView());

    pCommandList->SetGraphicsRootSignature(pRootSignature.Get());

    auto passCB = currFrameResource->PassCB->Resource();
    pCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    //pCommandList->SetPipelineState(PSOs["opaque"].Get());
    DrawRendeItems(pCommandList.Get(), renderItemLayer[(int)RenderLayer::Opaque]);

    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
    ));

    ThrowIfFailed(pCommandList->Close());

    ID3D12CommandList* cmdLists[] = { pCommandList.Get() };
    pCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    ThrowIfFailed(pSwapChain->Present(0U, 0U));
    currBackBuffer = (currBackBuffer + 1) % SwapChainBufferCount;

    currFrameResource->fenceVal = ++currentFence;

    pCommandQueue->Signal(pFence.Get(), currentFence);
}

void LandAndWavesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    lastMousePos.x = x;
    lastMousePos.y = y;

    SetCapture(mainHWnd);
}

void LandAndWavesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void LandAndWavesApp::OnMouseMove(WPARAM btnState, int x, int y)
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
        float dx = 0.05f * static_cast<float>(x - lastMousePos.x);
        float dy = 0.05f * static_cast<float>(y - lastMousePos.y);

        // Update the camera radius based on input.
        radius += dx - dy;

        // Restrict the radius.
        radius = MathHelper::Clamp(radius, 5.0f, 150.0f);
    }

    lastMousePos.x = x;
    lastMousePos.y = y;
}

void LandAndWavesApp::OnkeyboardInput(const GameTimer& gt)
{
    if (GetAsyncKeyState('1') & 0x8000)
        isWireframe = true;
    else
        isWireframe = false;
}

void LandAndWavesApp::UpdateCamera(const GameTimer& gt)
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

void LandAndWavesApp::UpdateObjectCBs(const GameTimer& gf)
{
    auto currObjectCB = currFrameResource->ObjectCB.get();
    for (auto& e : allRItems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if (e->NumFramesDirty > 0)
        {
            DirectX::XMMATRIX world = XMLoadFloat4x4(&e->World);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void LandAndWavesApp::UpdateMainPassCB(const GameTimer& gt)
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

void LandAndWavesApp::UpdateWaves(const GameTimer& gt)
{
    using namespace DirectX;
    // Every quarter second, generate a random wave.
    static float t_base = 0.0f;
    if ((timer.TotalTime() - t_base) >= 0.25f)
    {
        t_base += 0.25f;

        int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
        int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

        float r = MathHelper::RandF(0.2f, 0.5f);

        mWaves->Disturb(i, j, r);
    }

    // Update the wave simulation.
    mWaves->Update(gt.DeltaTime());

    // Update the wave vertex buffer with the new solution.
    auto currWavesVB = currFrameResource->WavesVB.get();
    for (int i = 0; i < mWaves->VertexCount(); ++i)
    {
        Vertex v;

        v.Pos = mWaves->Position(i);
        v.Color = XMFLOAT4(DirectX::Colors::Blue);

        currWavesVB->CopyData(i, v);
    }

    // Set the dynamic VB of the wave renderitem to the current frame VB.
    wavesRItem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void LandAndWavesApp::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER rootParams[2]{};
    rootParams[0].InitAsConstantBufferView(0U);
    rootParams[1].InitAsConstantBufferView(1U);

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

void LandAndWavesApp::BuildShadersAndInputLayout()
{
    ThrowIfFailed(D3DReadFileToBlob(L"Shaders\\ShaderBins\\standardVS.cso", shaders["standardVS"].GetAddressOf()));
    ThrowIfFailed(D3DReadFileToBlob(L"Shaders\\ShaderBins\\opaquePS.cso", shaders["opaquePS"].GetAddressOf()));

    inputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void LandAndWavesApp::BuildLandGeometry()
{
    using namespace DirectX;

    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

    //
    // Extract the vertex elements we are interested and apply the height function to
    // each vertex.  In addition, color the vertices based on their height so we have
    // sandy looking beaches, grassy low hills, and snow mountain peaks.
    //

    std::vector<Vertex> vertices(grid.Vertices.size());
    for (size_t i = 0; i < grid.Vertices.size(); ++i)
    {
        auto& p = grid.Vertices[i].Position;
        vertices[i].Pos = p;
        vertices[i].Pos.y = GetHillsHeight(p.x, p.z);

        // Color the vertex based on its height.
        if (vertices[i].Pos.y < -10.0f)
        {
            // Sandy beach color.
            vertices[i].Color = XMFLOAT4(1.0f, 0.96f, 0.62f, 1.0f);
        }
        else if (vertices[i].Pos.y < 5.0f)
        {
            // Light yellow-green.
            vertices[i].Color = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
        }
        else if (vertices[i].Pos.y < 12.0f)
        {
            // Dark yellow-green.
            vertices[i].Color = XMFLOAT4(0.1f, 0.48f, 0.19f, 1.0f);
        }
        else if (vertices[i].Pos.y < 20.0f)
        {
            // Dark brown.
            vertices[i].Color = XMFLOAT4(0.45f, 0.39f, 0.34f, 1.0f);
        }
        else
        {
            // White snow.
            vertices[i].Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    std::vector<std::uint16_t> indices = grid.GetIndices16();
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "landGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(pDevice.Get(),
        pCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(pDevice.Get(),
        pCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    geometries["landGeo"] = std::move(geo);
}

void LandAndWavesApp::BuildWavesGeometryBuffers()
{
    std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
    assert(mWaves->VertexCount() < 0x0000ffff);

    // Iterate over each quad.
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for (int i = 0; i < m - 1; ++i)
    {
        for (int j = 0; j < n - 1; ++j)
        {
            indices[k] = i * n + j;
            indices[k + 1] = i * n + j + 1;
            indices[k + 2] = (i + 1) * n + j;

            indices[k + 3] = (i + 1) * n + j;
            indices[k + 4] = i * n + j + 1;
            indices[k + 5] = (i + 1) * n + j + 1;

            k += 6; // next quad
        }
    }

    UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
    UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "waterGeo";

    // Set dynamically.
    geo->VertexBufferCPU = nullptr;
    geo->VertexBufferGPU = nullptr;

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(pDevice.Get(),
        pCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    geometries["waterGeo"] = std::move(geo);
}

void LandAndWavesApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

    //
    // PSO for opaque objects.
    //
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
    opaquePsoDesc.pRootSignature = pRootSignature.Get();
    opaquePsoDesc.VS = CD3DX12_SHADER_BYTECODE(shaders["standardVS"].Get());
    opaquePsoDesc.PS = CD3DX12_SHADER_BYTECODE(shaders["opaquePS"].Get());
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = backBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    opaquePsoDesc.DSVFormat = depthStencilFormat;
    ThrowIfFailed(pDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&PSOs["opaque"])));


    //
    // PSO for opaque wireframe objects.
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
    opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(pDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&PSOs["opaque_wireframe"])));
}

void LandAndWavesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; i++)
    {
        frameResources.push_back(std::make_unique<FrameResource>(pDevice.Get(), 1, (UINT)allRItems.size(), mWaves->VertexCount()));
    }
}

void LandAndWavesApp::BuildRenderItems()
{
    using namespace DirectX;

    auto wavesRitem = std::make_unique<RenderItem>();
    wavesRitem->World = MathHelper::Identity4x4();
    wavesRitem->ObjCBIndex = 0;
    wavesRitem->Geo = geometries["waterGeo"].get();
    wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
    wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    wavesRItem = wavesRitem.get();

    renderItemLayer[(int)RenderLayer::Opaque].push_back(wavesRitem.get());

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    gridRitem->ObjCBIndex = 1;
    gridRitem->Geo = geometries["landGeo"].get();
    gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    renderItemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

    allRItems.push_back(std::move(wavesRitem));
    allRItems.push_back(std::move(gridRitem));
}

void LandAndWavesApp::DrawRendeItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& rItems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto objectCB = currFrameResource->ObjectCB->Resource();

    // For each render item...
    for (size_t i = 0; i < rItems.size(); ++i)
    {
        auto ri = rItems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
        objCBAddress += ri->ObjCBIndex * objCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

float LandAndWavesApp::GetHillsHeight(float x, float z) const
{
    return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

DirectX::XMFLOAT3 LandAndWavesApp::GetHillsNormal(float x, float z) const
{
    using namespace DirectX;
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
        1.0f,
        -0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}

