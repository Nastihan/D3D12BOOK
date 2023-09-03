#include "LandAndWavesApp.h"
#include "Common/GeometryGenerator.h"

//const int gNumFrameResources = 3;

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
}

void LandAndWavesApp::Draw(const GameTimer& gt)
{
    ThrowIfFailed(pCmdListAlloc->Reset());
    ThrowIfFailed(pCommandList->Reset(pCmdListAlloc.Get(), nullptr));

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

    ID3D12DescriptorHeap* descriptorHeaps[] = { pCbvHeap.Get() };
    pCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    pCommandList->SetGraphicsRootSignature(pRootSignature.Get());

    pCommandList->SetGraphicsRootDescriptorTable(1, CD3DX12_GPU_DESCRIPTOR_HANDLE(
        pCbvHeap->GetGPUDescriptorHandleForHeapStart(), passCbvOffset, cbvSrvUavDescriptorSize
    ));
    pCommandList->SetPipelineState(PSOs["opaque"].Get());
    DrawRendeItems(pCommandList.Get(), opaqueRItems);

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

void LandAndWavesApp::BuildDescriptorHeaps()
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

void LandAndWavesApp::BuildConstantBufferViews()
{
    UINT objCbByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT objCount = (UINT)opaqueRItems.size();

    for (int frameIndex = 0; frameIndex < gNumFrameResources; frameIndex++)
    {
        auto objectCB = frameResources[frameIndex]->ObjectCB->Resource();
        for (UINT i = 0; i < objCount; i++)
        {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

            cbAddress += i * objCbByteSize;

            int heapIndex = frameIndex * objCount + i;
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


void LandAndWavesApp::BuildRootSignature()
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

void LandAndWavesApp::BuildShapeGeometry()
{
    using namespace DirectX;

    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    //
    // We are concatenating all the geometry into one big vertex/index buffer.  So
    // define the regions in the buffer each submesh covers.
    //

    // Cache the vertex offsets to each object in the concatenated vertex buffer.
    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

    // Cache the starting index for each object in the concatenated index buffer.
    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

    // Define the SubmeshGeometry that cover different 
    // regions of the vertex/index buffers.

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = boxIndexOffset;
    boxSubmesh.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
    gridSubmesh.StartIndexLocation = gridIndexOffset;
    gridSubmesh.BaseVertexLocation = gridVertexOffset;

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

    //
    // Extract the vertex elements we are interested in and pack the
    // vertices of all the meshes into one vertex buffer.
    //

    auto totalVertexCount =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size();

    std::vector<Vertex> vertices(totalVertexCount);

    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGreen);
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
    }

    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

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

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;

    geometries[geo->Name] = std::move(geo);

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
    opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
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
        frameResources.push_back(std::make_unique<FrameResource>(pDevice.Get(), 1, (UINT)allRItems.size()));
    }
}

void LandAndWavesApp::BuildRenderItems()
{
    using namespace DirectX;

    auto boxRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    boxRitem->ObjCBIndex = 0;
    boxRitem->Geo = geometries["shapeGeo"].get();
    boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
    boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    allRItems.push_back(std::move(boxRitem));

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    gridRitem->ObjCBIndex = 1;
    gridRitem->Geo = geometries["shapeGeo"].get();
    gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    allRItems.push_back(std::move(gridRitem));

    UINT objCBIndex = 2;
    for (int i = 0; i < 5; ++i)
    {
        auto leftCylRitem = std::make_unique<RenderItem>();
        auto rightCylRitem = std::make_unique<RenderItem>();
        auto leftSphereRitem = std::make_unique<RenderItem>();
        auto rightSphereRitem = std::make_unique<RenderItem>();

        XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
        XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

        XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
        XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

        XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
        leftCylRitem->ObjCBIndex = objCBIndex++;
        leftCylRitem->Geo = geometries["shapeGeo"].get();
        leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
        rightCylRitem->ObjCBIndex = objCBIndex++;
        rightCylRitem->Geo = geometries["shapeGeo"].get();
        rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
        leftSphereRitem->ObjCBIndex = objCBIndex++;
        leftSphereRitem->Geo = geometries["shapeGeo"].get();
        leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

        XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
        rightSphereRitem->ObjCBIndex = objCBIndex++;
        rightSphereRitem->Geo = geometries["shapeGeo"].get();
        rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

        allRItems.push_back(std::move(leftCylRitem));
        allRItems.push_back(std::move(rightCylRitem));
        allRItems.push_back(std::move(leftSphereRitem));
        allRItems.push_back(std::move(rightSphereRitem));
    }

    // All the render items are opaque.
    for (auto& e : allRItems)
        opaqueRItems.push_back(e.get());
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

        // Offset to the CBV in the descriptor heap for this object and for this frame resource.
        UINT cbvIndex = currFrameResourceIndex * (UINT)opaqueRItems.size() + ri->ObjCBIndex;
        auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(pCbvHeap->GetGPUDescriptorHandleForHeapStart());
        cbvHandle.Offset(cbvIndex, cbvSrvUavDescriptorSize);

        cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

