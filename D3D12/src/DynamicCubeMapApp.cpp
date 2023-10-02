#include "DynamicCubeMapApp.h"
#include "Common/GeometryGenerator.h"


DynamicCubeMapApp::DynamicCubeMapApp(HINSTANCE hInstance)
    :D3DApp(hInstance)
{
}

DynamicCubeMapApp::~DynamicCubeMapApp()
{
}

bool DynamicCubeMapApp::Initialize()
{
    if (!D3DApp::Initialize())
    {
        return false;
    }

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(pCommandList->Reset(pCmdListAlloc.Get(), nullptr));

    cam.SetPosition({ 0.0f, 2.0f, -5.0f });
    //cam.LookAt(DirectX::XMFLOAT3{ 0.0f, 2.0f, -10.0f }, { 0.0f, 0.0f, 10.0f }, { 0.0f, 1.0f, 0.0f });
    BuildCubeMapCameras(0.0f, 2.0f, 0.0f);
  
    cubeMap = std::make_unique<CubeRenderTarget>(pDevice.Get(), 512U, 512U, DXGI_FORMAT_R8G8B8A8_UNORM);

    cbvSrvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildCubeDepthStencil();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildSkullGeometry();
    BuildMaterials();
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

void DynamicCubeMapApp::OnResize()
{
    D3DApp::OnResize();


    cam.SetLens(45.0f, GetAR(), 0.5f, 400.0f);
    
}

void DynamicCubeMapApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    
    currFrameResourceIndex = (currFrameResourceIndex + 1) % gNumFrameResources;
    currFrameResource = frameResources[currFrameResourceIndex].get();

    if (pFence->GetCompletedValue() < currFrameResource->fenceVal)
    {
        HANDLE eventHanlde = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(pFence->SetEventOnCompletion(currFrameResource->fenceVal, eventHanlde));
        WaitForSingleObject(eventHanlde, INFINITE);
        CloseHandle(eventHanlde);
    }

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialBuffer(gt);
    UpdateMainPassCB(gt);
    UpdateCubeMapPassCB(gt);
}

void DynamicCubeMapApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = currFrameResource->pCmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(pCommandList->Reset(cmdListAlloc.Get(), PSOs["opaque"].Get()));

    ID3D12DescriptorHeap* descriptorHeaps[] = { pSrvDescriptorHeap.Get() };
    pCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

   

    pCommandList->SetGraphicsRootSignature(pRootSignature.Get());

    // Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and 
    // set as a root descriptor.
    auto matBuffer = currFrameResource->MaterialBuffer->Resource();
    pCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

    // Bind the sky cube map.  For our demos, we just use one "world" cube map representing the environment
    // from far away, so all objects will use the same cube map and we only need to set it once per-frame.  
    // If we wanted to use "local" cube maps, we would have to change them per-object, or dynamically
    // index into an array of cube maps.

    CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(pSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    skyTexDescriptor.Offset(3, cbvSrvUavDescriptorSize);
    pCommandList->SetGraphicsRootDescriptorTable(4, skyTexDescriptor);

    // Bind all the textures used in this scene.  Observe
    // that we only have to specify the first descriptor in the table.  
    // The root signature knows how many descriptors are expected in the table.
    pCommandList->SetGraphicsRootDescriptorTable(3, pSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    DrawToCubeMap();

    
    pCommandList->RSSetViewports(1, &screenViewport);
    pCommandList->RSSetScissorRects(1, &scissorRect);

    // Indicate a state transition on the resource usage.
    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    pCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::LightSteelBlue, 0, nullptr);
    pCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    pCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    auto passCB = currFrameResource->PassCB->Resource();
    pCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());


    // Use the dynamic cube map for the dynamic reflectors layer.
    CD3DX12_GPU_DESCRIPTOR_HANDLE dynamicTexDescriptor(pSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    dynamicTexDescriptor.Offset(3 + 1, cbvSrvUavDescriptorSize);
    pCommandList->SetGraphicsRootDescriptorTable(4, dynamicTexDescriptor);

    DrawRenderItems(pCommandList.Get(), rItemLayer[(int)RenderLayer::DynamicReflector]);

    // Use the static "background" cube map for the other objects (including the sky)
    pCommandList->SetGraphicsRootDescriptorTable(4, skyTexDescriptor);

    DrawRenderItems(pCommandList.Get(), rItemLayer[(int)RenderLayer::Opaque]);

    pCommandList->SetPipelineState(PSOs["sky"].Get());
    DrawRenderItems(pCommandList.Get(), rItemLayer[(int)RenderLayer::Sky]);

    // Indicate a state transition on the resource usage.
    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(pCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { pCommandList.Get() };
    pCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(pSwapChain->Present(0, 0));
    currBackBuffer = (currBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    currFrameResource->fenceVal = ++currentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    pCommandQueue->Signal(pFence.Get(), currentFence);
}

void DynamicCubeMapApp::DrawToCubeMap()
{
    
    pCommandList->RSSetViewports(1, &cubeMap->Viewport());
    pCommandList->RSSetScissorRects(1, &cubeMap->ScissorRect());

    // Change to RENDER_TARGET.
    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(cubeMap->Resource(),
        D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

    // For each cube map face.
    for (int i = 0; i < 6; ++i)
    {
        // Clear the back buffer and depth buffer.
        pCommandList->ClearRenderTargetView(cubeMap->Rtv(i), DirectX::Colors::LightSteelBlue, 0, nullptr);
        pCommandList->ClearDepthStencilView(cubeDSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

        // Specify the buffers we are going to render to.
        pCommandList->OMSetRenderTargets(1, &cubeMap->Rtv(i), true, &cubeDSV);

        // Bind the pass constant buffer for this cube map face so we use 
        // the right view/proj matrix for this cube face.
        auto passCB = currFrameResource->PassCB->Resource();
        D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + (1 + i) * passCBByteSize;
        pCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

        DrawRenderItems(pCommandList.Get(), rItemLayer[(int)RenderLayer::Opaque]);

        pCommandList->SetPipelineState(PSOs["sky"].Get());
        DrawRenderItems(pCommandList.Get(), rItemLayer[(int)RenderLayer::Sky]);

        pCommandList->SetPipelineState(PSOs["opaque"].Get());
    }

    // Change back to GENERIC_READ so we can read the texture in a shader.
    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(cubeMap->Resource(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));

}

void DynamicCubeMapApp::CreateRtvAndDsvDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 6;
    rtvHeapDesc.NodeMask = 0;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&pRtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.NumDescriptors = 2;
    dsvHeapDesc.NodeMask = 0;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(pDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&pDsvHeap)));

    cubeDSV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        pDsvHeap->GetCPUDescriptorHandleForHeapStart(),
        1,
        dsvDescriptorSize);
}

void DynamicCubeMapApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    lastMousePos.x = x;
    lastMousePos.y = y;

    SetCapture(mainHWnd);
}

void DynamicCubeMapApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void DynamicCubeMapApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    using namespace DirectX;
    if ((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - lastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - lastMousePos.y));

        cam.Pitch(dy);
        cam.RotateY(dx);
    }

    lastMousePos.x = x;
    lastMousePos.y = y;
}

void DynamicCubeMapApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState('W') & 0x8000)
        cam.Walk(10.0f * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        cam.Walk(-10.0f * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        cam.Strafe(-10.0f * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        cam.Strafe(10.0f * dt);

    cam.UpdateViewMatrix();
}

void DynamicCubeMapApp::AnimateMaterials(const GameTimer& gt)
{
    // animate the skull
    using namespace DirectX;
    XMMATRIX world(
        XMMatrixScaling(0.2f, 0.2f, 0.2f) * 
        XMMatrixRotationY(gt.TotalTime()) * 
        XMMatrixTranslation(0, 2.5f, -2.5) * 
        XMMatrixRotationY(0.3f * gt.TotalTime()) 
        );
    
    XMStoreFloat4x4(&skullRItem->World, world);
    skullRItem->NumFramesDirty = gNumFrameResources;
}

void DynamicCubeMapApp::UpdateObjectCBs(const GameTimer& gf)
{
    auto currObjectCB = currFrameResource->ObjectCB.get();
    for (auto& e : allRItems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if (e->NumFramesDirty > 0)
        {
            DirectX::XMMATRIX world = XMLoadFloat4x4(&e->World);
            DirectX::XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
            objConstants.MaterialIndex = e->Mat->MatCBIndex;

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void DynamicCubeMapApp::UpdateMaterialBuffer(const GameTimer& gt)
{
    using namespace DirectX;
    auto currMaterialBuffer = currFrameResource->MaterialBuffer.get();
    for (auto& e : materials)
    {
        // Only update the cbuffer data if the constants have changed.  If the cbuffer
        // data changes, it needs to be updated for each FrameResource.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialData matData;
            matData.DiffuseAlbedo = mat->DiffuseAlbedo;
            matData.FresnelR0 = mat->FresnelR0;
            matData.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
            matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

            currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

            // Next FrameResource need to be updated too.
            mat->NumFramesDirty--;
        }
    }
}

void DynamicCubeMapApp::UpdateMainPassCB(const GameTimer& gt)
{
    DirectX::XMMATRIX view = cam.GetView();
    DirectX::XMMATRIX proj = cam.GetProj();

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
    mainPassCB.EyePosW = cam.GetPosition3f();
    mainPassCB.RenderTargetSize = DirectX::XMFLOAT2((float)clientWidth, (float)clientHeight);
    mainPassCB.InvRenderTargetSize = DirectX::XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
    mainPassCB.NearZ = 1.0f;
    mainPassCB.FarZ = 1000.0f;
    mainPassCB.TotalTime = gt.TotalTime();
    mainPassCB.DeltaTime = gt.DeltaTime();
    mainPassCB.AmbientLight = { 0.05f, 0.05f, 0.10f, 1.0f };
    mainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
    mainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
    mainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

    auto currPassCB = currFrameResource->PassCB.get();
    currPassCB->CopyData(0, mainPassCB);
}

void DynamicCubeMapApp::UpdateCubeMapPassCB(const GameTimer& gt)
{
    for (int i = 0; i < 6; i++)
    {
        DirectX::XMMATRIX view = cubeMapCameras[i].GetView();
        DirectX::XMMATRIX proj = cubeMapCameras[i].GetProj();

        DirectX::XMMATRIX viewProj = XMMatrixMultiply(view, proj);
        DirectX::XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
        DirectX::XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
        DirectX::XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

        DirectX::XMStoreFloat4x4(&cubeMapPassCBs[i].View, XMMatrixTranspose(view));
        DirectX::XMStoreFloat4x4(&cubeMapPassCBs[i].InvView, XMMatrixTranspose(invView));
        DirectX::XMStoreFloat4x4(&cubeMapPassCBs[i].Proj, XMMatrixTranspose(proj));
        DirectX::XMStoreFloat4x4(&cubeMapPassCBs[i].InvProj, XMMatrixTranspose(invProj));
        DirectX::XMStoreFloat4x4(&cubeMapPassCBs[i].ViewProj, XMMatrixTranspose(viewProj));
        DirectX::XMStoreFloat4x4(&cubeMapPassCBs[i].InvViewProj, XMMatrixTranspose(invViewProj));
        cubeMapPassCBs[i].EyePosW = cubeMapCameras[i].GetPosition3f();
        cubeMapPassCBs[i].RenderTargetSize = DirectX::XMFLOAT2((float)clientWidth, (float)clientHeight);
        cubeMapPassCBs[i].InvRenderTargetSize = DirectX::XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
        cubeMapPassCBs[i].NearZ = 1.0f;
        cubeMapPassCBs[i].FarZ = 1000.0f;
        cubeMapPassCBs[i].TotalTime = gt.TotalTime();
        cubeMapPassCBs[i].DeltaTime = gt.DeltaTime();
        cubeMapPassCBs[i].AmbientLight = { 0.05f, 0.05f, 0.10f, 1.0f };
        cubeMapPassCBs[i].Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
        cubeMapPassCBs[i].Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
        cubeMapPassCBs[i].Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
        cubeMapPassCBs[i].Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
        cubeMapPassCBs[i].Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
        cubeMapPassCBs[i].Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

        auto currPassCB = currFrameResource->PassCB.get();
        currPassCB->CopyData(i + 1, cubeMapPassCBs[i]);
    }
}

void DynamicCubeMapApp::LoadTextures()
{
    std::vector<std::string> texNames =
    {
        "bricksDiffuseMap",
        "tileDiffuseMap",
        "defaultDiffuseMap",
        "skyCubeMap"
    };

    std::vector<std::wstring> texFilenames =
    {
        L"Textures/bricks2.dds",
        L"Textures/tile.dds",
        L"Textures/white1x1.dds",
        L"Textures/grasscube1024.dds"
    };

    for (int i = 0; i < (int)texNames.size(); ++i)
    {
        auto texMap = std::make_unique<Texture>();
        texMap->Name = texNames[i];
        texMap->Filename = texFilenames[i];
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(pDevice.Get(),
            pCommandList.Get(), texMap->Filename.c_str(),
            texMap->Resource, texMap->UploadHeap));

        textures[texMap->Name] = std::move(texMap);
    }
}

void DynamicCubeMapApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable{};
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1U, 0U);
    CD3DX12_DESCRIPTOR_RANGE cubeMapTable{};
    cubeMapTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0U, 0U);

    CD3DX12_ROOT_PARAMETER rootParams[5]{};
    rootParams[0].InitAsConstantBufferView(0U);
    rootParams[1].InitAsConstantBufferView(1U);
    rootParams[2].InitAsShaderResourceView(0u, 1u);
    rootParams[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[4].InitAsDescriptorTable(1, &cubeMapTable, D3D12_SHADER_VISIBILITY_PIXEL);

    auto samplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc((UINT)std::size(rootParams), rootParams,
        (UINT)samplers.size(), samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

void DynamicCubeMapApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.NumDescriptors = 5;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NodeMask = 0;
    ThrowIfFailed(pDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&pSrvDescriptorHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorH(pSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto bricksTex = textures["bricksDiffuseMap"]->Resource;
    auto tileTex = textures["tileDiffuseMap"]->Resource;
    auto whiteTex = textures["defaultDiffuseMap"]->Resource;
    auto skyTex = textures["skyCubeMap"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = bricksTex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    pDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, descriptorH);

    // next
    descriptorH.Offset(1, cbvSrvDescriptorSize);

    srvDesc.Format = tileTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
    pDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, descriptorH);

    // next
    descriptorH.Offset(1, cbvSrvDescriptorSize);

    srvDesc.Format = whiteTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = whiteTex->GetDesc().MipLevels;
    pDevice->CreateShaderResourceView(whiteTex.Get(), &srvDesc, descriptorH);

    // next
    descriptorH.Offset(1, cbvSrvDescriptorSize);

    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Format = skyTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = skyTex->GetDesc().MipLevels;
    pDevice->CreateShaderResourceView(skyTex.Get(), &srvDesc, descriptorH);

    //mSkyTexHeapIndex = 3;
    //mDynamicTexHeapIndex = mSkyTexHeapIndex + 1;

    auto srvCpuStart = pSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    auto srvGpuStart = pSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    auto rtvCpuStart = pRtvHeap->GetCPUDescriptorHandleForHeapStart();

    // Cubemap RTV goes after the swap chain descriptors.
    int rtvOffset = SwapChainBufferCount;

    CD3DX12_CPU_DESCRIPTOR_HANDLE cubeRtvHandles[6];
    for (int i = 0; i < 6; ++i)
        cubeRtvHandles[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvCpuStart, rtvOffset + i, rtvDescriptorSize);

    // Dynamic cubemap SRV is after the sky SRV.
    cubeMap->BuildDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, 4, cbvSrvUavDescriptorSize),
        CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, 4, cbvSrvUavDescriptorSize),
        cubeRtvHandles);
}

void DynamicCubeMapApp::BuildCubeDepthStencil()
{
    // Create the depth/stencil buffer and view.
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = clientWidth;
    depthStencilDesc.Height = clientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = depthStencilFormat;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = depthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;
    ThrowIfFailed(pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(cubeDepthStencilBuffer.GetAddressOf())));

    // Create descriptor to mip level 0 of entire resource using the format of the resource.
    pDevice->CreateDepthStencilView(cubeDepthStencilBuffer.Get(), nullptr, cubeDSV);

    // Transition the resource from its initial state to be used as a depth buffer.
    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(cubeDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
}

void DynamicCubeMapApp::BuildShadersAndInputLayout()
{
    ThrowIfFailed(D3DReadFileToBlob(L"Shaders\\ShaderBins\\DefaultVS.cso", shaders["standardVS"].GetAddressOf()));
    ThrowIfFailed(D3DReadFileToBlob(L"Shaders\\ShaderBins\\DefaultPS.cso", shaders["opaquePS"].GetAddressOf()));

    shaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\ShaderFiles\\Sky.hlsl", nullptr, "VS", "vs_5_1");
    shaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\ShaderFiles\\Sky.hlsl", nullptr, "PS", "ps_5_1");

    inputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
}

void DynamicCubeMapApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
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
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].TexC = box.Vertices[i].TexC;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexC;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexC;
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

void DynamicCubeMapApp::BuildSkullGeometry()
{
    using namespace DirectX;
    std::ifstream fin("Models/skull.txt");

    if (!fin)
    {
        MessageBox(0, L"Models/skull.txt not found.", 0, 0);
        return;
    }

    UINT vcount = 0;
    UINT tcount = 0;
    std::string ignore;

    fin >> ignore >> vcount;
    fin >> ignore >> tcount;
    fin >> ignore >> ignore >> ignore >> ignore;

    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

    std::vector<Vertex> vertices(vcount);
    for (UINT i = 0; i < vcount; ++i)
    {
        fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
        fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

        vertices[i].TexC = { 0.0f, 0.0f };

        XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

        vMin = XMVectorMin(vMin, P);
        vMax = XMVectorMax(vMax, P);
    }

    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

    fin >> ignore;
    fin >> ignore;
    fin >> ignore;

    std::vector<std::int32_t> indices(3 * tcount);
    for (UINT i = 0; i < tcount; ++i)
    {
        fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
    }

    fin.close();

    //
    // Pack the indices of all the meshes into one index buffer.
    //

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";

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
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    submesh.Bounds = bounds;

    geo->DrawArgs["skull"] = submesh;

    geometries[geo->Name] = std::move(geo);
}

void DynamicCubeMapApp::BuildPSOs()
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

    auto skyMapPsoDesc = opaquePsoDesc;
    skyMapPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    skyMapPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    skyMapPsoDesc.VS = CD3DX12_SHADER_BYTECODE(shaders["skyVS"].Get());
    skyMapPsoDesc.PS = CD3DX12_SHADER_BYTECODE(shaders["skyPS"].Get());

    ThrowIfFailed(pDevice->CreateGraphicsPipelineState(&skyMapPsoDesc, IID_PPV_ARGS(&PSOs["sky"])));

}

void DynamicCubeMapApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(pDevice.Get(), 7, (UINT)allRItems.size(), (UINT)materials.size()));
    }
}

void DynamicCubeMapApp::BuildMaterials()
{
    using namespace DirectX;
    auto bricks0 = std::make_unique<Material>();
    bricks0->Name = "bricks0";
    bricks0->MatCBIndex = 0;
    bricks0->DiffuseSrvHeapIndex = 0;
    bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    bricks0->Roughness = 0.3f;

    auto tile0 = std::make_unique<Material>();
    tile0->Name = "tile0";
    tile0->MatCBIndex = 1;
    tile0->DiffuseSrvHeapIndex = 1;
    tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
    tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    tile0->Roughness = 0.1f;

    auto mirror0 = std::make_unique<Material>();
    mirror0->Name = "mirror0";
    mirror0->MatCBIndex = 2;
    mirror0->DiffuseSrvHeapIndex = 2;
    mirror0->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
    mirror0->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
    mirror0->Roughness = 0.1f;

    auto sky = std::make_unique<Material>();
    sky->Name = "sky";
    sky->MatCBIndex = 3;
    sky->DiffuseSrvHeapIndex = 3;
    sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    sky->Roughness = 1.0f;

    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 4;
    skullMat->DiffuseSrvHeapIndex = 2;
    skullMat->DiffuseAlbedo = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    skullMat->Roughness = 0.2f;

    materials["bricks0"] = std::move(bricks0);
    materials["tile0"] = std::move(tile0);
    materials["mirror0"] = std::move(mirror0);
    materials["sky"] = std::move(sky);
    materials["skullMat"] = std::move(skullMat);
}

void DynamicCubeMapApp::BuildRenderItems()
{
    using namespace DirectX;

    auto skyRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
    skyRitem->TexTransform = MathHelper::Identity4x4();
    skyRitem->ObjCBIndex = 0;
    skyRitem->Mat = materials["sky"].get();
    skyRitem->Geo = geometries["shapeGeo"].get();
    skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
    skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

    rItemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
    allRItems.push_back(std::move(skyRitem));

    auto skullRitem = std::make_unique<RenderItem>();
    skullRitem->World = MathHelper::Identity4x4();
    skullRitem->TexTransform = MathHelper::Identity4x4();
    skullRitem->ObjCBIndex = 1;
    skullRitem->Mat = materials["skullMat"].get();
    skullRitem->Geo = geometries["skullGeo"].get();
    skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;

    skullRItem = skullRitem.get();

    rItemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());
    allRItems.push_back(std::move(skullRitem));

    auto boxRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    boxRitem->ObjCBIndex = 2;
    boxRitem->Mat = materials["bricks0"].get();
    boxRitem->Geo = geometries["shapeGeo"].get();
    boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
    boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

    rItemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
    allRItems.push_back(std::move(boxRitem));

    auto globeRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&globeRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 2.0f, 0.0f));
    XMStoreFloat4x4(&globeRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    globeRitem->ObjCBIndex = 3;
    globeRitem->Mat = materials["mirror0"].get();
    globeRitem->Geo = geometries["shapeGeo"].get();
    globeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    globeRitem->IndexCount = globeRitem->Geo->DrawArgs["sphere"].IndexCount;
    globeRitem->StartIndexLocation = globeRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    globeRitem->BaseVertexLocation = globeRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

    rItemLayer[(int)RenderLayer::DynamicReflector].push_back(globeRitem.get());
    allRItems.push_back(std::move(globeRitem));

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
    gridRitem->ObjCBIndex = 4;
    gridRitem->Mat = materials["tile0"].get();
    gridRitem->Geo = geometries["shapeGeo"].get();
    gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    rItemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
    allRItems.push_back(std::move(gridRitem));

    XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
    UINT objCBIndex = 5;
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
        XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
        leftCylRitem->ObjCBIndex = objCBIndex++;
        leftCylRitem->Mat = materials["bricks0"].get();
        leftCylRitem->Geo = geometries["shapeGeo"].get();
        leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
        XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
        rightCylRitem->ObjCBIndex = objCBIndex++;
        rightCylRitem->Mat = materials["bricks0"].get();
        rightCylRitem->Geo = geometries["shapeGeo"].get();
        rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
        leftSphereRitem->TexTransform = MathHelper::Identity4x4();
        leftSphereRitem->ObjCBIndex = objCBIndex++;
        leftSphereRitem->Mat = materials["mirror0"].get();
        leftSphereRitem->Geo = geometries["shapeGeo"].get();
        leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

        XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
        rightSphereRitem->TexTransform = MathHelper::Identity4x4();
        rightSphereRitem->ObjCBIndex = objCBIndex++;
        rightSphereRitem->Mat = materials["mirror0"].get();
        rightSphereRitem->Geo = geometries["shapeGeo"].get();
        rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
         
        rItemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
        rItemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
        rItemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
        rItemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());

        allRItems.push_back(std::move(leftCylRitem));
        allRItems.push_back(std::move(rightCylRitem));
        allRItems.push_back(std::move(leftSphereRitem));
        allRItems.push_back(std::move(rightSphereRitem));
    }
}

void DynamicCubeMapApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& rItems)
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

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);


        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void DynamicCubeMapApp::BuildCubeMapCameras(float x, float y, float z)
{
    using namespace DirectX;
    DirectX::XMFLOAT3 targets[6]
    {
        XMFLOAT3(x + 1.0f, y, z), // +X
        XMFLOAT3(x - 1.0f, y, z), // -X
        XMFLOAT3(x, y + 1.0f, z), // +Y
        XMFLOAT3(x, y - 1.0f, z), // -Y
        XMFLOAT3(x, y, z + 1.0f), // +Z
        XMFLOAT3(x, y, z - 1.0f)  // -Z
    };
    DirectX::XMFLOAT3 ups[6]
    {
        XMFLOAT3(0.0f, 1.0f, 0.0f),  // +X
        XMFLOAT3(0.0f, 1.0f, 0.0f),  // -X
        XMFLOAT3(0.0f, 0.0f, -1.0f), // +Y
        XMFLOAT3(0.0f, 0.0f, +1.0f), // -Y
        XMFLOAT3(0.0f, 1.0f, 0.0f),	 // +Z
        XMFLOAT3(0.0f, 1.0f, 0.0f)	 // -Z
    };
    for (size_t i = 0; i < 6; i++)
    {
        cubeMapCameras[i].LookAt({ x, y, z }, targets[i], ups[i]);
        cubeMapCameras[i].SetLens(0.5f * DirectX::XM_PI, 1.0f, 0.1f, 1000.0f);
        cubeMapCameras[i].UpdateViewMatrix();
    }

}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> DynamicCubeMapApp::GetStaticSamplers()
{
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.  

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
        0.0f,                             // mipLODBias
        16U);                               // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
        0.0f,                              // mipLODBias
        16U);                                // maxAnisotropy

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp };
}

