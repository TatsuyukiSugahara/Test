#pragma once


namespace app
{
	namespace ecs
	{
		/**
		 * コイン 1 枚ぶんのデータ。位置 / 姿勢は TransformComponent 側が持ち、
		 * ここには取得判定と回転演出に必要なものだけを置く
		 * (設計: Game/Design/AquaDash/02_ECS設計.md)。
		 */
		struct CoinComponent : public aq::ecs::IComponent
		{
			ecsComponent(app::ecs::CoinComponent);

			/** 取得判定 */
			float distance  = 0.0f;   // スプライン距離 (粗い絞り込み用。生成側が焼き込む)
			bool  collected = false;

			/** 回転演出 */
			float                spinPhase    = 0.0f;                            // 回転位相 [rad]
			aq::math::Quaternion baseRotation = aq::math::Quaternion::Identity;   // 生成時の路面姿勢
		};




		/**
		 * プレイヤー 1 人分のスコア集計。リザルト評価の元データ
		 */
		struct PlayerScoreComponent : public aq::ecs::IComponent
		{
			ecsComponent(app::ecs::PlayerScoreComponent);

			uint32_t coinCount = 0;
			uint32_t fallCount = 0;
		};




		/**
		 * コインの回転演出と取得判定。
		 * 取得したコインは破棄せず非表示にするだけに留め、リザルトの「もう一度」で
		 * ReactivateAll から復活させる (ECS の構造変更を起こさない)。
		 */
		class CoinSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;


		public:
			/** 全コインを未取得 + 表示に戻す (「もう一度」用) */
			static void ReactivateAll();
		};
	}
}
