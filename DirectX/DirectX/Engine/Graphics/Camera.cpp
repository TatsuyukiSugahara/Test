#include "../EnginePreCompile.h"
#include "Camera.h"
#include "../Engine.h"

namespace engine
{
	Camera::Camera()
		: near_(1.0f)
		, far_(1000.0f)
		, viewAngle_(math::DegToRadian(90.0f))
		, acpect_(0.0f)
		, position_(math::Vector3::Zero)
		, targetPosition_(math::Vector3::Zero)
		, viewMatrix_(math::Matrix4x4::Identity)
		, projectionMatrix_(math::Matrix4x4::Identity)
		, viewProjectionMatrix_(math::Matrix4x4::Identity)
		, cameraRotation_(math::Matrix4x4::Identity)
		, viewMatrixInverse_(math::Matrix4x4::Identity)
	{
	}


	Camera::~Camera()
	{
	}


	void Camera::Update()
	{
		float aspect = static_cast<float>(Engine::Get().GetRenderWidth() / Engine::Get().GetRenderHeight());
		projectionMatrix_.MakeProjectionMatrix(viewAngle_, aspect, near_, far_);

		// �r���[�s��v�Z
		viewMatrix_.MakeLookAt(position_, targetPosition_, math::Vector3::Up);
		// �r���[�v���W�F�N�V�����s��v�Z
		viewProjectionMatrix_.Mull(viewMatrix_, projectionMatrix_);
		// �r���[�s��̋t�s��v�Z
		viewMatrixInverse_.Inverse(viewMatrix_);
		// �J�����̉�]�s��擾
		cameraRotation_ = viewMatrixInverse_;
		cameraRotation_.m[3][0] = 0.0f;
		cameraRotation_.m[3][1] = 0.0f;
		cameraRotation_.m[3][2] = 0.0f;
		cameraRotation_.m[3][3] = 1.0f;
	}




	/*******************************************/


	CameraManager* CameraManager::sInstance_ = nullptr;


	CameraManager::CameraManager()
		: cameras_()
	{
	}


	CameraManager::~CameraManager()
	{
	}
}