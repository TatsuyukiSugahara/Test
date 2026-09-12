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
		 * タイトル画面 (R-14)。ロゴ / ステージサムネイル / ステージ名 / PRESS 点滅を表示する。
		 * ステージ名とサムネイルの内容は StageDefinition 側から流し込む。
		 */
		class TitleScreen : public aq::ui::UIScreen
		{
		private:
			float             elapsed_    = 0.0f;
			aq::ui::UIObject* press_      = nullptr;
			aq::ui::UIObject* stageLabel_ = nullptr;
			aq::ui::UIObject* stageThumb_ = nullptr;


		public:
			void OnEnter()          override;
			void OnUpdate(float dt) override;

			/** ステージ表示ラベルの内容を差し替える (例: "STAGE 01    GREEN COAST") */
			void SetStageName(const char* text);

			/**
			 * ステージサムネイル画像を差し替える (StageList.json の thumbnail)。
			 * パスが空 / ロードできない場合は枠ごと非表示のままにする。
			 */
			void SetStageThumbnail(const char* path);
		};




		/**
		 * インゲーム HUD (R-14)。右上=経過時間 / 左下=コイン枚数 / 右下=速度 / 左上=ミニマップを表示する。
		 * コイン枚数の上にはコンボ中だけスコア倍率と残り時間ゲージを出す。
		 * 画面中央やや上にはエアトリック中だけ回転数を出す。
		 * 値の計算は InGameState 側が行い、本クラスは見た目の反映のみ担当する。
		 */
		class InGameScreen : public aq::ui::UIScreen
		{
		private:
			/** HUD テキスト */
			aq::ui::UIObject* timeText_  = nullptr;
			aq::ui::UIObject* coinText_  = nullptr;
			aq::ui::UIObject* speedText_ = nullptr;

			/** コンボ表示 (倍率テキストと残り時間ゲージ) */
			aq::ui::UIObject* comboText_  = nullptr;
			aq::ui::UIObject* comboGauge_ = nullptr;

			/** エアトリック表示 (滞空中の回転数テキスト) */
			aq::ui::UIObject* trickText_ = nullptr;
			/** ゴーストとの時間差表示 (P23。ゴーストが無い走行では非表示) */
			aq::ui::UIObject* ghostDeltaText_ = nullptr;

			/** ミニマップ (枠 / 俯瞰ベイク画像 / プレイヤーマーカー) */
			aq::ui::UIObject* minimapFrame_  = nullptr;
			aq::ui::UIObject* minimap_       = nullptr;
			aq::ui::UIObject* minimapMarker_ = nullptr;


		public:
			void OnEnter() override;

			/**
			 * HUD 表示の反映 (毎フレーム呼ばれる)
			 * @param timeSec         InGame 突入からの経過時間 (秒)
			 * @param coinCount       取得済みコイン枚数
			 * @param speedKmh        表示用に km/h 換算済みの速度
			 * @param comboMultiplier 現在のスコア倍率 (1 = コンボ無し。このとき倍率表示は隠す)
			 * @param comboRate       コンボ継続の残り時間の割合 (0-1)
			 * @param trickCount      この滞空で完了した回転数
			 * @param trickActive     滞空中にトリックが進行中か (false のときトリック表示は隠す)
			 */
			void SetHUD(const float timeSec, const uint32_t coinCount, const float speedKmh,
			            const uint32_t comboMultiplier, const float comboRate,
			            const uint32_t trickCount, const bool trickActive,
			            const float ghostDeltaSec, const bool hasGhost);

			/**
			 * ミニマップに表示する俯瞰ベイク画像を差し替える。
			 * nullptr を渡すとミニマップ全体を隠す。
			 */
			void SetMinimapTexture(const std::shared_ptr<aq::graphics::IShaderResourceView>& texture);

			/** ミニマップ上のプレイヤーマーカー位置。u,v は 0-1 (ミニマップ矩形ローカル。u=右+, v=下+) */
			void SetMinimapMarker(const float u, const float v);
		};




		/**
		 * リザルト画面 (P0 仮)。結果ヘッダ + タイム / コイン / スコア / 最大コンボ + ランク + 3 択メニューを表示する。
		 * カーソル操作と決定判定は ResultState 側が行い、本クラスは見た目の反映のみ担当する。
		 */
		class ResultScreen : public aq::ui::UIScreen
		{
		private:
			aq::ui::UIObject* header_    = nullptr;
			aq::ui::UIObject* time_      = nullptr;
			aq::ui::UIObject* coin_      = nullptr;
			aq::ui::UIObject* score_     = nullptr;
			aq::ui::UIObject* bestCombo_ = nullptr;
			aq::ui::UIObject* rank_      = nullptr;
			aq::ui::UIObject* items_[RESULT_MENU_COUNT] = {};


		public:
			void OnEnter() override;

			/**
			 * 結果表示の反映
			 * @param cleared   true=ステージクリア / false=ゲームオーバー
			 * @param timeSec   クリアタイム (秒)
			 * @param coinCount 取得済みコイン枚数
			 * @param score     コンボ倍率込みの合計スコア
			 * @param bestCombo このプレイでの最大コンボ数
			 * @param rank      ランク文字 ("S"/"A"/"B"/"C")。ゲームオーバー時は nullptr または空文字
			 */
			void SetResult(const bool cleared, const float timeSec, const uint32_t coinCount,
			               const uint32_t score, const uint32_t bestCombo, const char* rank);

			/** メニューカーソル位置の反映 (0=もう一度 1=次へ 2=タイトルへ) */
			void SetCursor(const int index);
		};
	}
}
