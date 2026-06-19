#pragma once
#include "Utility.h"
#include "ECS/ECS.h"
#include "Graphics/Camera.h"

namespace app
{
	namespace ecs
	{
		enum class CameraLookAtMode : uint8_t
		{
			TrackEntity,    // EntityHandle の TransformComponent 位置を追従
			FixedPosition,  // 固定ワールド座標
		};

		enum class CameraPositionMode : uint8_t
		{
			OffsetFromTarget,  // 注視点からのオフセット（FollowOnly 時のみ参照）
			FixedPosition,     // 固定ワールド座標（FollowOnly 時のみ参照）
		};

		enum class CameraControlMode : uint8_t
		{
			FollowOnly,  // 入力なしで対象を追従
			ManualView,  // Look 入力で視点の左右/上下角度を操作
		};


		struct CameraSteeringComponent : aq::ecs::IComponent
		{
			ecsComponent(app::ecs::CameraSteeringComponent);


		public:
			// --- 注視点 ---
			CameraLookAtMode      lookAtMode     = CameraLookAtMode::FixedPosition;
			aq::ecs::EntityHandle lookAtEntity;
			aq::math::Vector3     lookAtPosition = {};  // 固定座標 or Entity への加算オフセット


		public:
			// --- カメラ位置（FollowOnly 時のみ参照。ManualView 時は無視される）---
			CameraPositionMode positionMode       = CameraPositionMode::OffsetFromTarget;
			aq::math::Vector3  cameraOffset       = { 0.0f, 5.0f, -10.0f };
			bool               offsetInLocalSpace = true;
			aq::math::Vector3  fixedPosition      = {};


		public:
			// --- 視点操作（ManualView 時のみ有効）---
			CameraControlMode controlMode                = CameraControlMode::FollowOnly;
			float             viewYawDegrees             = 0.0f;
			float             viewPitchDegrees           = 20.0f;
			float             distanceFromTarget         = 10.0f;
			float             yawSpeedDegreesPerSecond   = 120.0f;
			float             pitchSpeedDegreesPerSecond = 80.0f;
			float             minViewPitchDegrees        = -10.0f;
			float             maxViewPitchDegrees        =  60.0f;
			bool              invertViewY                = true;


		public:
			// --- 制御 ---
			aq::CameraType cameraType = aq::CameraType::Main;
			bool           isActive   = true;
			int            priority   = 0;  // 同 CameraType で複数 Active のとき高い方が優先


		public:
			// --- スムージング（秒ベース。大きいほど速く追従）---
			float lookAtSharpness   = 8.0f;
			float positionSharpness = 6.0f;


		public:
			// --- スムージング状態（System が書き込む）---
			aq::math::Vector3 smoothedTarget       = {};
			aq::math::Vector3 smoothedPosition     = {};
			bool              smoothingInitialized = false;


		public:
			~CameraSteeringComponent() = default;

		public:
			// LookAt: EntityHandle を追従（offset は Entity 位置への加算）
			void TrackEntity(const aq::ecs::EntityHandle& handle, const aq::math::Vector3& offset = {})
			{
				lookAtMode     = CameraLookAtMode::TrackEntity;
				lookAtEntity   = handle;
				lookAtPosition = offset;
			}

			// LookAt: 固定座標を注視
			void LookAtPoint(const aq::math::Vector3& pos)
			{
				lookAtMode     = CameraLookAtMode::FixedPosition;
				lookAtPosition = pos;
			}

			// FollowOnly: 注視点からのオフセットでカメラ位置を決定
			void SetOffsetFromTarget(const aq::math::Vector3& offset, const bool localSpace = true)
			{
				controlMode        = CameraControlMode::FollowOnly;
				positionMode       = CameraPositionMode::OffsetFromTarget;
				cameraOffset       = offset;
				offsetInLocalSpace = localSpace;
			}

			// FollowOnly: カメラを固定座標に配置
			void SetFixedCameraPosition(const aq::math::Vector3& pos)
			{
				controlMode   = CameraControlMode::FollowOnly;
				positionMode  = CameraPositionMode::FixedPosition;
				fixedPosition = pos;
			}

			// ManualView: Look 入力で視点操作するモードに切替
			void SetManualView(const float initialYaw = 0.0f, const float initialPitch = 20.0f, const float distance = 10.0f)
			{
				controlMode        = CameraControlMode::ManualView;
				viewYawDegrees     = initialYaw;
				viewPitchDegrees   = initialPitch;
				distanceFromTarget = std::max(distance, 0.01f);
			}
		};


		class CameraSteeringSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;

#ifdef AQ_DEBUG_IMGUI
			void DebugRenderMenu() override;
			void DebugRender() override;
#endif


		private:
#ifdef AQ_DEBUG_IMGUI
			bool show_ = false;
#endif
		};


		// -----------------------------------------------------------------------

		struct CameraEffectComponent : aq::ecs::IComponent
		{
			ecsComponent(app::ecs::CameraEffectComponent);


		public:
			aq::CameraType cameraType = aq::CameraType::Main;
			bool           isActive   = true;


		public:
			// --- シェイク ---
			float shakeMagnitude = 0.2f;
			float shakeDuration  = 0.0f;  // 残り時間（秒）。0 以下でシェイクなし


		public:
			// --- シェイク状態（System が書き込む）---
			aq::math::Vector3 shakeOffset = {};


		public:
			void TriggerShake(const float duration)
			{
				shakeDuration = std::max(duration, 0.0f);
			}
		};


		class CameraEffectSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;

#ifdef AQ_DEBUG_IMGUI
			void DebugRenderMenu() override;
			void DebugRender() override;
#endif


		private:
#ifdef AQ_DEBUG_IMGUI
			bool show_ = false;
#endif
		};
	}
}
