#pragma once
#include "Math/Bounds.h"

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
	public:
		/**
		 * カメラの状態の写し（位置・注視点・Up・Near・Far・画角・正射影）。
		 * アスペクト比とビューポートは含めない（ゲームビューの大きさは復元と無関係に変わるため）。
		 * 行列は持たず、RestoreState のあとの次の Update で作り直される。
		 */
		struct State
		{
			math::Vector3 position;
			math::Vector3 target;
			math::Vector3 up           = math::Vector3::Up;
			float         nearPlane    = 1.0f;
			float         farPlane     = 1000.0f;
			float         viewAngle    = 0.0f;
			bool          orthographic = false;
			float         orthographicWidth  = 0.0f;
			float         orthographicHeight = 0.0f;
		};


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

		/** 現在の状態を写し取る（アスペクト比は含まない） */
		State CaptureState() const;
		/** 写し取った状態へ戻す。行列は次の Update で作り直される（アスペクト比は変えない） */
		void  RestoreState(const State& state);


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

		/**
		 * ビューポートの中の正規化座標からワールドのレイを作る。
		 * (u, v) は左上 (0,0) 〜 右下 (1,1)。始点は近平面上の点、direction は正規化済み。
		 * 行列は Update が作ったもの(アスペクト比の変更は次の Update から反映される)
		 */
		math::Ray ScreenPointToRay(float u, float v) const;
		/**
		 * ワールドの点をクリップ空間(同次座標。w で割る前)へ写す。深度は [0, 1]。
		 * 線を近い面(z = 0)で切ってから w で割りたいとき用
		 */
		math::Vector4 WorldToClip(const math::Vector3& worldPosition) const;

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