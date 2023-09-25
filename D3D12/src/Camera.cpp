#include "Camera.h"

Camera::Camera()
{
}

void Camera::SetPos(DirectX::XMFLOAT3 pos)
{
	this->pos = pos;
	auto eyePos = DirectX::XMVectorSet(pos.x, pos.y, pos.z, 1.0f);
	auto forward = DirectX::XMVectorSet( 0.0f, 0.0f, 1.0f, 0.0f );
	auto target = DirectX::XMVectorAdd(eyePos, forward);
	auto up = DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
	DirectX::XMStoreFloat4x4(&view, DirectX::XMMatrixLookAtLH(eyePos, target, up));
}

void Camera::SetProj(float fovY, float aspectRatio, float nearZ, float farZ)
{
	this->fovY = fovY;
	this->aspectRatio = aspectRatio;
	this->nearZ = nearZ;
	this->farZ = farZ;

	DirectX::XMStoreFloat4x4(&proj, DirectX::XMMatrixPerspectiveFovLH(fovY, aspectRatio, nearZ, farZ));
}

DirectX::XMFLOAT4X4 Camera::GetView()
{
	return view;
}

DirectX::XMFLOAT4X4 Camera::GetProj()
{
	return proj;
}

DirectX::XMMATRIX Camera::GetViewM()
{
	auto v = DirectX::XMLoadFloat4x4(&view);
	return v;
}

DirectX::XMMATRIX Camera::GetProjM()
{
	auto p = DirectX::XMLoadFloat4x4(&proj);
	return p;
}
