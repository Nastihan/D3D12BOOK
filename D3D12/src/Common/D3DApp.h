#pragma once
#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"
#include "GameTimer.h"

class D3DApp
{
protected:
	D3DApp(HINSTANCE hInstance);
	D3DApp(const D3DApp& rhs) = delete; 
	D3DApp& operator=(const D3DApp& rhs) = delete;
	virtual ~D3DApp();
public:
	static D3DApp* GetApp();
	HINSTANCE GetAppInst() const;
	HWND GetMainWnd() const;
	float GetAR() const;

	bool Get4xMsaaState()const;
	void Set4xMsaaState(bool value);

	int Run();

	virtual bool Initialize();
	virtual LRESULT MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
protected:
	virtual void CreateRtvAndDsvDescriptorHeaps();
	virtual void OnResize();
	virtual void Update(const GameTimer& gt) = 0;
	virtual void Draw(const GameTimer& gt) = 0;
	// Convenience overrides for handling mouse input.
	virtual void OnMouseDown(WPARAM btnState, int x, int y) { }
	virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
	virtual void OnMouseMove(WPARAM btnState, int x, int y) { }
protected:
	bool InitMainWindow();
	bool InitDirect3D();
	void CreateCommandObjects();
	void CreateSwapChain();

	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer()const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()const;

	void CalculateFrameStats();

	void LogAdapters();
	void LogAdapterOutputs(IDXGIAdapter* adapter);
	void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);
protected:
	static D3DApp* pApp;

	HINSTANCE appHInst = nullptr; // application instance handle
	HWND mainHWnd = nullptr; // main window handle
	bool appPaused = false;  
	bool minimized = false;  
	bool maximized = false;  
	bool resizing = false;   
	bool fullscreenState = false;// fullscreen enabled

	// Set true to use 4X MSAA (§4.1.8).  The default is false.
	bool      m4xMsaaState = false;    // 4X MSAA enabled
	UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

	// Used to keep track of the “delta-time” and game time (§4.4).
	GameTimer timer;

	Microsoft::WRL::ComPtr<IDXGIFactory4> pDxgiFactory;
	Microsoft::WRL::ComPtr<IDXGISwapChain> pSwapChain;
	Microsoft::WRL::ComPtr<ID3D12Device> pDevice;

	Microsoft::WRL::ComPtr<ID3D12Fence> pFence;
	UINT64 currentFence = 0;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> pCommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> pCmdListAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pCommandList;

	static const int SwapChainBufferCount = 2;
	int currBackBuffer = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> pSwapChainBuffer[SwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> pDepthStencilBuffer;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pRtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pDsvHeap;

	D3D12_VIEWPORT screenViewport;
	D3D12_RECT scissorRect;

	UINT rtvDescriptorSize = 0;
	UINT dsvDescriptorSize = 0;
	UINT cbvSrvUavDescriptorSize = 0;

	// Derived class should set these in derived constructor to customize starting values.
	std::wstring mainWndCaption = L"d3d12 App";
	D3D_DRIVER_TYPE d3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int clientWidth = 1366;
	int clientHeight = 768;
};