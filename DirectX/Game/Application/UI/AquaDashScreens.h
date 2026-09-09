#pragma once
#include "UI/Screen/UIScreen.h"
#include "Math/Vector.h"


namespace aq { namespace graphics { class IShaderResourceView; } }

namespace app
{
	namespace aquadash
	{
		/** リザルトメニューの項目数 (もう一度 / 次へ / タイトルへ) */
		static constexpr int RESULT_MENU_COUNT = 3;


		/**
		 * タイトル画面 (R-14)。ロゴ / ステージ名 / PRESS 点滅を表示する。
		 * ステージ名の内容は StageDefinition 側から SetStageName で流し込む。
		 */
		class TitleScreen : public aq::ui::UIScreen
		{
		private:
			float             elapsed_    = 0.0f;
			aq::ui::UIObject* press_      = nullptr;
			aq::ui::UIObject* stageLabel_ = nullptr;


		public:
			void OnEnter()          override;
			void OnUpdate(float dt) override;

			/** ステージ表示ラベルの内容を差し替える (例: "STAGE 01    GREEN COAST") */
			void SetStageName(const char* text);
		};




		/**
		 * インゲーム HUD (R-14)。右上=経過時間 / 左下=コイン枚数 / 右下=速度 / 左上=ミニマップを表示する。
		 * 値の計算は InGameState 側が行い、本クラスは見た目の反映のみ担当する。
		 */
		class InGameScreen : public aq::ui::UIScreen
		{
		private:
			/** HUD テキスト */
			aq::ui::UIObject* timeText_  = nullptr;
			aq::ui::UIObject* coinText_  = nullptr;
			aq::ui::UIObject* speedText_ = nullptr;

			/** ミニマップ (枠 / 俯瞰ベイク画像 / プレイヤーマーカー) */
			aq::ui::UIObject* minimapFrame_  = nullptr;
			aq::ui::UIObject* minimap_       = nullptr;
			aq::ui::UIObject* minimapMarker_ = nullptr;


		public:
			void OnEnter() override;

			/**
			 * HUD 表示の反映 (毎フレーム呼ばれる)
			 * @param timeSec   InGame 突入からの経過時間 (秒)
			 * @param coinCount 取得済みコイン枚数
			 * @param speedKmh  表示用に km/h 換算済みの速度
			 */
			void SetHUD(const float timeSec, const uint32_t coinCount, const float speedKmh);

			/**
			 * ミニマップに表示する俯瞰ベイク画像を差し替える。
			 * nullptr を渡すとミニマップ全体を隠す。
			 */
			void SetMinimapTexture(const std::shared_ptr<aq::graphics::IShaderResourceView>& texture);

			/** ミニマップ上のプレイヤーマーカー位置。u,v は 0-1 (ミニマップ矩形ローカル。u=右+, v=下+) */
			void SetMinimapMarker(const float u, const float v);
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
