#pragma once
#include "UI/Screen/UIScreen.h"


namespace app
{
	namespace aquadash
	{
		/** リザルトメニューの項目数 (もう一度 / 次へ / タイトルへ) */
		static constexpr int RESULT_MENU_COUNT = 3;


		/**
		 * タイトル画面 (P0 仮)。PRESS テキストの点滅のみ行う。
		 * 本デザイン (ステージ選択リスト / 参加プレイヤー表示) は P7 で差し替える。
		 */
		class TitleScreen : public aq::ui::UIScreen
		{
		private:
			float             elapsed_ = 0.0f;
			aq::ui::UIObject* press_   = nullptr;


		public:
			void OnEnter()          override;
			void OnUpdate(float dt) override;
		};




		/**
		 * インゲーム HUD (R-14)。右上=経過時間 / 左下=コイン枚数 / 右下=速度を表示する。
		 * 値の計算は InGameState 側が行い、本クラスは見た目の反映のみ担当する。
		 * 左上のミニマップは P7 で追加する。
		 */
		class InGameScreen : public aq::ui::UIScreen
		{
		private:
			/** HUD テキスト */
			aq::ui::UIObject* timeText_  = nullptr;
			aq::ui::UIObject* coinText_  = nullptr;
			aq::ui::UIObject* speedText_ = nullptr;


		public:
			void OnEnter() override;

			/**
			 * HUD 表示の反映 (毎フレーム呼ばれる)
			 * @param timeSec   InGame 突入からの経過時間 (秒)
			 * @param coinCount 取得済みコイン枚数
			 * @param speedKmh  表示用に km/h 換算済みの速度
			 */
			void SetHUD(const float timeSec, const uint32_t coinCount, const float speedKmh);
		};




		/**
		 * リザルト画面 (P0 仮)。結果ヘッダ + タイム + 3 択メニューを表示する。
		 * カーソル操作と決定判定は ResultState 側が行い、本クラスは見た目の反映のみ担当する。
		 */
		class ResultScreen : public aq::ui::UIScreen
		{
		private:
			aq::ui::UIObject* header_ = nullptr;
			aq::ui::UIObject* time_   = nullptr;
			aq::ui::UIObject* coin_   = nullptr;
			aq::ui::UIObject* rank_   = nullptr;
			aq::ui::UIObject* items_[RESULT_MENU_COUNT] = {};


		public:
			void OnEnter() override;

			/**
			 * 結果表示の反映
			 * @param cleared   true=ステージクリア / false=ゲームオーバー
			 * @param timeSec   クリアタイム (秒)
			 * @param coinCount 取得済みコイン枚数
			 * @param rank      ランク文字 ("S"/"A"/"B"/"C")。ゲームオーバー時は nullptr または空文字
			 */
			void SetResult(const bool cleared, const float timeSec, const uint32_t coinCount, const char* rank);

			/** メニューカーソル位置の反映 (0=もう一度 1=次へ 2=タイトルへ) */
			void SetCursor(const int index);
		};
	}
}
