#pragma once

namespace aq
{
	enum class CameraType : uint8_t
	{
		Main,
		Offscreen,
		Maximum,
	};


	class Camera
	{
	private:
		math::Vector3 position_;
		math::Vector3 targetPosition_;
		math::Vector3 up_ = math::Vector3::Up;   // ビューの上方向 (ループ走行などロールが必要なカメラ用)
		math::Matrix4x4 viewMatrix_;
		math::Matrix4x4 projectionMatrix_;
		math::Matrix4x4 viewProjectionMatrix_;
		math::Matrix4x4 viewMatrixInverse_;
		math::Matrix4x4 cameraRotation_;
		float near_;
		float far_;
		float viewAngle_;
		float aspect_;

		/** 正射影モード (SetOrthographic で有効化。未設定なら従来どおり透視投影) */
		bool  orthographic_       = false;
		float orthographicWidth_  = 0.0f;
		float orthographicHeight_ = 0.0f;


	public:
		Camera();
		~Camera();


		/** 更新 */
		void Update();


	public:
		/** カメラ座標設定 */
		void SetPosition(const math::Vector3& position)
		{
			position_.Set(position);
		}
		/** 注視点設定 */
		void SetTarget(const math::Vector3& target)
		{
			targetPosition_.Set(target);
		}
		/** ビューの上方向設定 (正規化済みであること。既定は +Y) */
		void SetUp(const math::Vector3& up)
		{
			up_.Set(up);
		}
		/** 近平面設定 */
		void SetNear(const float n)
		{
			near_ = n;
		}
		/** 遠平面設定 */
		void SetFar(const float f)
		{
			far_ = f;
		}
		/** 画角設定 */
		void SetViewAngle(const float angle)
		{
			viewAngle_ = angle;
		}
		/** ビューポートサイズからアスペクト比を設定 */
		void SetViewportSize(float width, float height);
		/** アスペクト比を直接設定 */
		void SetAspect(float aspect);
		/**
		 * 正射影モードにして描画範囲 (ワールド単位) を設定する。
		 * 俯瞰ミニマップのように歪みなく全景を収めたいカメラで使う。
		 * @param width  横方向の描画範囲 [m]
		 * @param height 縦方向の描画範囲 [m]
		 */
		void SetOrthographic(const float width, const float height);

		/** カメラ座標取得 */
		const math::Vector3& GetPosition() const { return position_; }

		/** 近平面取得 */
		float GetNear() const { return near_; }
		/** 遠平面取得 */
		float GetFar()  const { return far_; }

		/** ビュー行列取得 */
		const math::Matrix4x4& GetViewMatrix() const
		{
			return viewMatrix_;
		}
		/** ビュー行列の逆行列取得 */
		const math::Matrix4x4& GetViewMatrixInverse() const
		{
			return viewMatrixInverse_;
		}
		/** プロジェクション行列取得 */
		const math::Matrix4x4& GetProjectionMatrix() const
		{
			return projectionMatrix_;
		}
		/** ビュープロジェクション行列取得 */
		const math::Matrix4x4& GetViewProjectionMatrix() const
		{
			return viewProjectionMatrix_;
		}
		/** カメラの回転行列取得 */
		const math::Matrix4x4& GetCameraRotation() const
		{
			return cameraRotation_;
		}

		/** カメラの前方向をXZ平面に投影して正規化 */
		math::Vector3 GetForwardXZ() const;
		/** カメラの右方向をXZ平面に投影して正規化 */
		math::Vector3 GetRightXZ() const;
		/** XZ入力（z=前後 / x=左右）をカメラ相対のワールド方向に変換して正規化（ゼロ入力時はゼロ返却） */
		math::Vector3 TransformMoveInput(const math::Vector3& input) const;
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
		Camera* GetCamera(CameraType type);
		void UpdateAll()
		{
			for (auto& cam : cameras_)
				cam.Update();
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