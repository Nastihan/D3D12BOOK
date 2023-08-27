#include "D3DApp.h"
#include <windowsx.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// Forward hwnd on because we can get messages (e.g., WM_CREATE)
	// before CreateWindow returns, and thus before mhMainWnd is valid.
	return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

D3DApp* D3DApp::pApp = nullptr;

D3DApp* D3DApp::GetApp()
{
	return pApp;
}

D3DApp::D3DApp(HINSTANCE hInstance)
	:appHInst(hInstance)
{
	assert(pApp == nullptr);
	pApp = this;
}

D3DApp::~D3DApp()
{
	if (pDevice != nullptr);
	FlushCommandQueue();
}

HINSTANCE D3DApp::GetAppInst() const
{
    return appHInst;
}

HWND D3DApp::GetMainWnd() const
{
	return mainHWnd;
}

float D3DApp::GetAR() const
{
	return static_cast<float>(clientWidth) / clientHeight;
}

bool D3DApp::Get4xMsaaState() const
{
	return m4xMsaaState;
}

void D3DApp::Set4xMsaaState(bool value)
{
	if (m4xMsaaState != value)
	{
		m4xMsaaState = value;

		// Recreate the swapchain and buffers with new multisample settings.
		CreateSwapChain();
		OnResize();
	}
}

int D3DApp::Run()
{
	MSG msg = { 0 };

	timer.Reset();

	while (msg.message != WM_QUIT)
	{
		// process windows messages if there are any
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		// do game stuff
		else
		{

		}

	}

}

