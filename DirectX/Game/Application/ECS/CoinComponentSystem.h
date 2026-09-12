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

			/** コンボ関連 */
			uint32_t comboCount = 0;      // 現在のコンボ数 (途切れると 0)
			float    comboTimer = 0.0f;   // コンボ継続の残り時間 [s]
			uint32_t bestCombo  = 0;      // このプレイでの最大コンボ数 (リザルト表示用)
			uint32_t score      = 0;      // コンボ倍率込みの合計スコア
		};




		/**
		 * コインの回転演出と取得判定。描画は Coins エンティティのインスタンス点を
		 * 毎フレーム組み直して行い、取得したコインは破棄せず未取得フラグを落とすだけに留めて、
		 * リザルトの「もう一度」で ReactivateAll から復活させる (ECS の構造変更を起こさない)。
		 */
		class CoinSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;


		public:
			/** 全コインを未取得に戻して描画を組み直す (「もう一度」用) */
			static void ReactivateAll();

			/**
			 * コンボ数からスコア倍率を求める (HUD 表示と取得時の加算で式を二重に持たないため)
			 * @param comboCount 現在のコンボ数 (0 = コンボ無し)
			 * @return 1 〜 COMBO_MAX_MULTIPLIER の倍率
			 */
			static uint32_t CalcMultiplier(const uint32_t comboCount);

			/**
			 * コンボ継続の猶予 [s]。HUD のゲージが残り時間を 0-1 へ正規化するのに要る。
			 * しきい値は .cpp の匿名 namespace にあるので、値を二重に持たないようここから引く。
			 */
			static float GetComboWindowSec();
		};
	}
}
