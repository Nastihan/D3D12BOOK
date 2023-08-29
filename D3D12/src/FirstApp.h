#pragma once
#include "Common/D3DApp.h"

class FirstApp : public D3DApp
{
public:
	FirstApp(HINSTANCE hInstance);
	~FirstApp();

	virtual bool Initialize() override;
private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;
};