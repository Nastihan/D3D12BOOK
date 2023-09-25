#pragma once
#include "Common/d3dUtil.h"

class Camera
{
public:
	Camera();
	~Camera() =  default;
	void SetPos(DirectX::XMFLOAT3 pos);
	void SetProj(float fovY, float aspectRatio, float nearZ, float farZ);
	DirectX::XMFLOAT4X4 GetView();
	DirectX::XMFLOAT4X4 GetProj();
	DirectX::XMMATRIX GetViewM();
	DirectX::XMMATRIX GetProjM();

private:
	DirectX::XMFLOAT3 pos = {};
	

	float fovY = {};
	float aspectRatio = {};
	float nearZ = {};
	float farZ ={};
	DirectX::XMFLOAT4X4 view{};
	DirectX::XMFLOAT4X4 proj{};

};