#pragma once
#include "Common/D3DApp.h"

class App : public D3DApp
{
public:
	App(HINSTANCE hInstance);
	~App();

	virtual bool Initialize() override;
private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;
};