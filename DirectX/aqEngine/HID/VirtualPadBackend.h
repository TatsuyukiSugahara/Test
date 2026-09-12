#pragma once
#include "HID/IPadBackend.h"
#include "HID/ITouchBackend.h"


namespace aq
{
	namespace hid
	{
		/**
		 * タッチを仮想パッドとして見せるパッドバックエンド
		 * 呼び出し側が毎フレーム更新する TouchState を読み、画面上の
		 * 仮想スティック / ボタンの当たり判定を経て PadState へ変換する。
		 * 当たり判定用の図形(Layout)だけを持ち、描画は呼び出し側の責務。
		 * プラットフォーム非依存(Android / iOS で共有する)。
		 */
		class VirtualPadBackend : public IPadBackend
		{
			/**
			 * レイアウト定義
			 */
		public:
			/**
			 * 当たり判定用の円
			 * 中心は画面サイズに対する正規化値 [0, 1]。
			 * 半径は画面高さに対する比(横幅基準にすると縦横比で歪むため)。
			 */
			struct Circle
			{
				float centerX = 0.0f;
				float centerY = 0.0f;
				float radius  = 0.0f;
			};

			/**
			 * 仮想パッドの当たり判定レイアウト
			 * 既定値は横持ち前提で「左下にスティック、右下に A / B」。
			 * 実機で触って調整する前提の初期値。
			 */
			struct Layout
			{
				/** 左スティック */
				Circle leftStick = { 0.16f, 0.72f, 0.13f };

				/** ボタン */
				Circle buttonA     = { 0.88f, 0.78f, 0.075f };
				Circle buttonB     = { 0.78f, 0.60f, 0.075f };
				Circle buttonStart = { 0.94f, 0.10f, 0.050f };
			};


		private:
			/**
			 * 入力元のタッチ状態(非所有。寿命は呼び出し側が持つ)
			 * nullptr なら「触れていない」扱い。
			 * ITouchBackend を直に叩かないのは、イベント駆動のラッチが取得で
			 * 消費されるため。毎フレーム 1 回だけ取得した状態を UI と共有して読む。
			 */
			const TouchState* touch_;

			/** 当たり判定 */
			Layout layout_;

			/** 左スティックを掴んでいる指の id(INVALID_TOUCH_ID なら未掴み) */
			int32_t stickTouchId_;


		public:
			explicit VirtualPadBackend(const TouchState* touch);


		public:
			void Poll(uint32_t index, PadState& out) override;

			// タッチには振動やトリガーの実体が無いため no-op。
			void SetVibration(uint32_t /*index*/, float /*left*/, float /*right*/) override {}
			void SetTriggerResistance(uint32_t /*index*/, PadAxis /*trigger*/,
			                          float /*startPos*/, float /*strength*/) override {}


			/**
			 * レイアウト
			 */
		public:
			/** 当たり判定レイアウト(描画側もこれを読んで表示位置を決める) */
			inline const Layout& GetLayout() const { return layout_; }
			inline void SetLayout(const Layout& layout) { layout_ = layout; }


		private:
			void UpdateStick  (const TouchState& touches, const float screenWidth, const float screenHeight, PadState& out);
			void UpdateButtons(const TouchState& touches, const float screenWidth, const float screenHeight, PadState& out) const;


		private:
			/** 指を掴んでいない状態。TouchPoint の未使用スロットと同じ値 */
			static constexpr int32_t INVALID_TOUCH_ID = -1;

			/** 円の内側判定。タッチ座標はクライアント左上原点のピクセル */
			static bool IsInside(const Circle& circle, const TouchPoint& point,
			                     const float screenWidth, const float screenHeight);

			/** 触れている点のうち id が一致するものを返す(無ければ nullptr) */
			static const TouchPoint* FindTouch(const TouchState& touches, const int32_t id);
		};
	}
}
