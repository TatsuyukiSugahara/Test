#pragma once


namespace app
{
	namespace ecs
	{
		enum class AutoCameraMode : uint8_t
		{
			Follow,   // スプライン後方から追走 (インゲーム)
			Orbit,    // 終了地点を低速で周回 (リザルト。P3 で使用)
		};


		/**
		 * スプライン準拠の自動カメラ (設計 02 §4)。プレイヤーの少し後方の
		 * スプライン点にカメラを置き、少し前方を注視する。ループでは up が
		 * スプライン準拠のため画面が自然にロールする。
		 */
		struct AutoCameraComponent : public aq::ecs::IComponent
		{
			ecsComponent(app::ecs::AutoCameraComponent);

			/** 追従対象 (SpeedCharacterComponent を持つエンティティ) */
			aq::ecs::EntityHandle targetHandle;
			AutoCameraMode        mode      = AutoCameraMode::Follow;
			uint32_t              viewIndex = 0;   // 分割画面のビュー番号 (= playerIndex)

			/** 配置パラメータ (スプライン距離/up 方向オフセット) */
			float backDistance  = 7.0f;    // 対象の何 m 後方に置くか
			float cameraHeight  = 2.5f;    // 路面からの高さ
			float aheadDistance = 15.0f;   // 注視点は何 m 前方か
			float lookHeight    = 1.2f;    // 注視点の高さ

			/** スムージング (大きいほど速く追従。最高速 83m/s の定常遅れ ≒ speed/sharpness) */
			float sharpness = 25.0f;

			/** 出力先 */
			aq::CameraType cameraType = aq::CameraType::Main;

			/** スムージング状態 (System が書き込む) */
			aq::math::Vector3 smoothedPosition = {};
			aq::math::Vector3 smoothedTarget   = {};
			aq::math::Vector3 smoothedUp       = { 0.0f, 1.0f, 0.0f };
			bool              initialized      = false;
		};




		/**
		 * 自動カメラの駆動。ポーズ中 (リザルト) も動き続ける
		 */
		class AutoCameraSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;
		};
	}
}
