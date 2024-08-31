#pragma once

namespace engine
{
	enum class CameraType : uint8_t
	{
		Main,
		Maximum,
	};


	class Camera
	{
	private:
		math::Vector3 position_;
		math::Vector3 targetPosition_;
		math::Matrix4x4 viewMatrix_;
		math::Matrix4x4 projectionMatrix_;
		math::Matrix4x4 viewProjectionMatrix_;
		math::Matrix4x4 viewMatrixInverse_;
		math::Matrix4x4 cameraRotation_;
		float near_;
		float far_;
		float viewAngle_;
		float acpect_;


	public:
		Camera();
		~Camera();


		/** �X�V */
		void Update();


	public:
		/** �J�������W�ݒ� */
		void SetPosition(const math::Vector3& position)
		{
			position_.Set(position);
		}
		/** �����_�ݒ� */
		void SetTarget(const math::Vector3& target)
		{
			targetPosition_.Set(target);
		}
		/** �ߕ��ʐݒ� */
		void SetNear(const float n)
		{
			near_ = n;
		}
		/** �����ʐݒ� */
		void SetFar(const float f)
		{
			far_ = f;
		}
		/** ��p�ݒ� */
		void SetViewAngle(const float angle)
		{
			viewAngle_ = angle;
		}

		/** �r���[�s��擾 */
		const math::Matrix4x4& GetViewMatrix() const
		{
			return viewMatrix_;
		}
		/** �r���[�s��̋t�s��擾 */
		const math::Matrix4x4& GetViewMatrixInverse() const
		{
			return viewMatrixInverse_;
		}
		/** �v���W�F�N�V�����s��擾 */
		const math::Matrix4x4& GetProjectionMatrix() const
		{
			return projectionMatrix_;
		}
		/** �r���[�v���W�F�N�V�����s��擾 */
		const math::Matrix4x4& GetViewProjectionMatrix() const
		{
			return viewProjectionMatrix_;
		}
		/** �J�����̉�]�s��擾 */
		const math::Matrix4x4& GetCameraRotation() const
		{
			return cameraRotation_;
		}
	};




	/*******************************************/


	class CameraManager
	{
	private:
		Camera cameras_[static_cast<uint8_t>(CameraType::Maximum)];

	private:
		CameraManager();
		~CameraManager();


	public:
		Camera* GetCamera(CameraType type)
		{
			return &cameras_[static_cast<uint8_t>(type)];
		}


	private:
		static CameraManager* sInstance_;

	public:
		static void Initialize()
		{
			if (sInstance_ == nullptr) {
				sInstance_ = new CameraManager();
			}
		}
		static CameraManager& Get()
		{
			return *sInstance_;
		}
		static void Finalize()
		{
			if (sInstance_) {
				delete sInstance_;
				sInstance_ = nullptr;
			}
		}
	};

}