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


	void Camera::SetOrthographic(const float width, const float height)
	{
		EngineAssertMsg(width  > 0.0f, "Camera::SetOrthographic: width must be > 0");
		EngineAssertMsg(height > 0.0f, "Camera::SetOrthographic: height must be > 0");
		if (width > 0.0f && height > 0.0f)
		{
			orthographic_       = true;
			orthographicWidth_  = width;
			orthographicHeight_ = height;
		}
	}


	Camera::State Camera::CaptureState() const
	{
		State state;
		state.position           = position_;
		state.target             = targetPosition_;
		state.up                 = up_;
		state.nearPlane          = near_;
		state.farPlane           = far_;
		state.viewAngle          = viewAngle_;
		state.orthographic       = orthographic_;
		state.orthographicWidth  = orthographicWidth_;
		state.orthographicHeight = orthographicHeight_;
		return state;
	}


	void Camera::RestoreState(const State& state)
	{
		// 行列は Update が毎回この値から作り直すので、ここで dirty を立てる必要はない
		position_           = state.position;
		targetPosition_     = state.target;
		up_                 = state.up;
		near_               = state.nearPlane;
		far_                = state.farPlane;
		viewAngle_          = state.viewAngle;
		orthographic_       = state.orthographic;
		orthographicWidth_  = state.orthographicWidth;
		orthographicHeight_ = state.orthographicHeight;
	}


	void Camera::Update()
	{
		// 正射影は SetOrthographic を呼んだカメラのみ。未設定カメラは従来どおり透視投影。
		if (orthographic_) {
			projectionMatrix_.MakeOrthographic(orthographicWidth_, orthographicHeight_, near_, far_);
		} else {
			projectionMatrix_.MakeProjectionMatrix(viewAngle_, aspect_, near_, far_);
		}

		// ビュー行列計算
		viewMatrix_.MakeLookAt(position_, targetPosition_, up_);
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




	math::Ray Camera::ScreenPointToRay(float u, float v) const
	{
		const float nx =  2.0f * u - 1.0f;
		const float ny = -(2.0f * v - 1.0f);

		math::Matrix4x4 vpInverse;
		vpInverse.Inverse(viewProjectionMatrix_);
		const DirectX::XMMATRIX xmVP = vpInverse;

		const DirectX::XMVECTOR nearClip = DirectX::XMVector4Transform(DirectX::XMVectorSet(nx, ny, 0.0f, 1.0f), xmVP);
		const DirectX::XMVECTOR farClip  = DirectX::XMVector4Transform(DirectX::XMVectorSet(nx, ny, 1.0f, 1.0f), xmVP);
		const float wNear = DirectX::XMVectorGetW(nearClip);
		const float wFar  = DirectX::XMVectorGetW(farClip);
		if (std::fabs(wNear) < 1e-12f || std::fabs(wFar) < 1e-12f)
		{
			// 行列が作られていない・特異。カメラの前方を返す
			math::Vector3 forward = targetPosition_ - position_;
			if (!forward.TryNormalize()) forward = math::Vector3(0.0f, 0.0f, 1.0f);
			return math::Ray(position_, forward);
		}

		DirectX::XMFLOAT3 nearPoint, farPoint;
		DirectX::XMStoreFloat3(&nearPoint, DirectX::XMVectorScale(nearClip, 1.0f / wNear));
		DirectX::XMStoreFloat3(&farPoint,  DirectX::XMVectorScale(farClip,  1.0f / wFar));

		const math::Vector3 origin(nearPoint.x, nearPoint.y, nearPoint.z);
		math::Vector3 direction = math::Vector3(farPoint.x, farPoint.y, farPoint.z) - origin;
		if (!direction.TryNormalize()) direction = math::Vector3(0.0f, 0.0f, 1.0f);
		return math::Ray(origin, direction);
	}


	math::Vector4 Camera::WorldToClip(const math::Vector3& worldPosition) const
	{
		const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(
			DirectX::XMVectorSet(worldPosition.x, worldPosition.y, worldPosition.z, 1.0f), viewProjectionMatrix_);
		return math::Vector4(DirectX::XMVectorGetX(clip), DirectX::XMVectorGetY(clip),
		                     DirectX::XMVectorGetZ(clip), DirectX::XMVectorGetW(clip));
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