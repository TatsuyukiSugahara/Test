#include "aq.h"
#include "Camera.h"

namespace aq
{
	Camera::Camera()
		: near_(1.0f)
		, far_(1000.0f)
		, viewAngle_(math::DegToRadian(90.0f))
		, aspect_(16.0f / 9.0f)
		, position_(0.0f, 0.0f, -1.0f)
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


	void Camera::SetViewportSize(float width, float height)
	{
		EngineAssertMsg(width > 0.0f,  "Camera::SetViewportSize: width must be > 0");
		EngineAssertMsg(height > 0.0f, "Camera::SetViewportSize: height must be > 0");
		if (width > 0.0f && height > 0.0f)
		{
			aspect_ = width / height;
		}
	}


	void Camera::SetAspect(float aspect)
	{
		EngineAssertMsg(aspect > 0.0f, "Camera::SetAspect: aspect must be > 0");
		if (aspect > 0.0f)
		{
			aspect_ = aspect;
		}
	}


	void Camera::Update()
	{
		projectionMatrix_.MakeProjectionMatrix(viewAngle_, aspect_, near_, far_);

		// ビュー行列計算
		viewMatrix_.MakeLookAt(position_, targetPosition_, math::Vector3::Up);
		// ビュープロジェクション行列計算
		viewProjectionMatrix_.Mull(viewMatrix_, projectionMatrix_);
		// ビュー行列の逆行列計算
		viewMatrixInverse_.Inverse(viewMatrix_);
		// カメラの回転行列取得
		cameraRotation_ = viewMatrixInverse_;
		cameraRotation_.m[3][0] = 0.0f;
		cameraRotation_.m[3][1] = 0.0f;
		cameraRotation_.m[3][2] = 0.0f;
		cameraRotation_.m[3][3] = 1.0f;
	}




	math::Vector3 Camera::GetForwardXZ() const
	{
		// 逆ビュー行列の行2 = カメラ前方（ワールド空間）
		math::Vector3 fwd(viewMatrixInverse_._31, 0.0f, viewMatrixInverse_._33);
		fwd.TryNormalize();
		return fwd;
	}


	math::Vector3 Camera::GetRightXZ() const
	{
		// 逆ビュー行列の行0 = カメラ右方向（ワールド空間）
		math::Vector3 right(viewMatrixInverse_._11, 0.0f, viewMatrixInverse_._13);
		right.TryNormalize();
		return right;
	}


	math::Vector3 Camera::TransformMoveInput(const math::Vector3& input) const
	{
		const math::Vector3 fwd   = GetForwardXZ();
		const math::Vector3 right = GetRightXZ();

		math::Vector3 result(
			fwd.x * input.z + right.x * input.x,
			0.0f,
			fwd.z * input.z + right.z * input.x);

		result.TryNormalize();
		return result;
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


	Camera* CameraManager::GetCamera(CameraType type)
	{
		const uint8_t index = static_cast<uint8_t>(type);
		const uint8_t max   = static_cast<uint8_t>(CameraType::Maximum);
		EngineAssert(index < max);
		if (index >= max)
		{
			return &cameras_[0];
		}
		return &cameras_[index];
	}
}