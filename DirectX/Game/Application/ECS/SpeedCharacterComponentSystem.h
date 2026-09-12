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

			/** ブースト状態 (パッドを踏むと一定時間だけ最高速が上がる) */
			float boostTimer   = 0.0f;   // 残りブースト秒
			float prevDistance = 0.0f;   // 跨ぎ判定用。前フレームの distance

			/** エアトリック状態 (滞空中に A を押すと進行方向軸まわりに回る) */
			float    trickSpinTimer      = 0.0f;   // 回転中の残り時間 [s]。0 なら回転していない
			uint32_t trickPendingCount   = 0;      // 積まれた回転数 (チェーン)
			uint32_t trickCompletedCount = 0;      // この滞空で完了した回転数
			float    trickRoll           = 0.0f;   // 見た目の回転角 [rad]

			/** 脱落状態 (ループで速度不足になった等。スプライン制御を離れワールド自由落下) */
			bool              fallen        = false;
			aq::math::Vector3 worldVelocity = {};   // fallen 中のワールド速度

			/** 再生中アニメの nameHash (実行時のみ。切替検知に使うのでシリアライズしない) */
			uint32_t currentAnimHash = 0;
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
