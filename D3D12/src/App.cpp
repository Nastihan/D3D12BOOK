#include "App.h"

App::App(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

App::~App()
{
}

bool App::Initialize()
{
    if (!D3DApp::Initialize())
    {
        return false;
    }
    return true;
}

void App::OnResize()
{
    D3DApp::OnResize();
}

void App::Update(const GameTimer& gt)
{
}

void App::Draw(const GameTimer& gt)
{

}
