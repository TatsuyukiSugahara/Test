#pragma once


namespace app
{
	namespace ecs
	{
		/**
		 * プレイヤーの入力状態。PlayerInputSystem が毎フレーム書き込む
		 * (一人プレイ専用: キーボードとパッド 0 を合成して読む)。
		 */
		struct PlayerInputComponent : public aq::ecs::IComponent
		{
			ecsComponent(app::ecs::PlayerInputComponent);

			/** 入力値 (System が書き込む) */
			float moveX         = 0.0f;   // レーン移動 (-1..1)
			float moveY         = 0.0f;   // 加減速 (-1..1)
			bool  jumpTriggered = false;
		};




		/**
		 * スプライン走行状態。位置はスプラインローカル座標 (distance/lateral/height) で持ち、
		 * SpeedCharacterSystem がワールド Transform へ書き出す
		 * (設計: Game/Design/AquaDash/02_ECS設計.md)。
		 */
		struct SpeedCharacterComponent : public aq::ecs::IComponent
		{
			ecsComponent(app::ecs::SpeedCharacterComponent);

			/** スプラインローカル座標 */
			float distance = 0.0f;
			float lateral  = 0.0f;
			float height   = 0.0f;

			/** 走行状態 */
			float speed            = 0.0f;   // 前進速度 [m/s]
			float verticalVelocity = 0.0f;
			bool  grounded         = true;

			/** 脱落状態 (ループで速度不足になった等。スプライン制御を離れワールド自由落下) */
			bool              fallen        = false;
			aq::math::Vector3 worldVelocity = {};   // fallen 中のワールド速度
		};




		/**
		 * 入力デバイス → PlayerInputComponent の転写
		 */
		class PlayerInputSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;
		};




		/**
		 * スプライン走行の積分 (加減速 / レーン移動 / ジャンプ / 重力) と
		 * ワールド Transform (位置 + 姿勢) の書き出し
		 */
		class SpeedCharacterSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;
		};
	}
}
