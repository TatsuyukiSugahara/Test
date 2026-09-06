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
		 * インゲーム HUD (P0 仮)。静的テキストのみ。HUD 本実装 (時間/コイン/速度) は P2。
		 */
		class InGameScreen : public aq::ui::UIScreen
		{
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
			aq::ui::UIObject* items_[RESULT_MENU_COUNT] = {};


		public:
			void OnEnter() override;

			/** 結果表示の反映 (クリア/ゲームオーバーと経過タイム) */
			void SetResult(const bool cleared, const float timeSec);

			/** メニューカーソル位置の反映 (0=もう一度 1=次へ 2=タイトルへ) */
			void SetCursor(const int index);
		};
	}
}
