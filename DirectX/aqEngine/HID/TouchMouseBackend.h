#pragma once
#include "HID/IMouseBackend.h"
#include "HID/ITouchBackend.h"


namespace aq
{
	namespace hid
	{
		/**
		 * タッチをポインタとして見せるマウスバックエンド
		 * 呼び出し側が毎フレーム更新する TouchState を読み、1 本目の指をカーソル、
		 * 触れている間を左ボタン押下として MouseState へ変換する。
		 * 物理マウスを想定するという意味ではなく、タッチを既存のポインタ経路
		 * (UIInputSystem / ImGui)へ載せるための合成である。
		 * プラットフォーム非依存(Android / iOS で共有する)。
		 */
		class TouchMouseBackend : public IMouseBackend
		{
		private:
			/**
			 * 入力元のタッチ状態(非所有。寿命は呼び出し側が持つ)
			 * nullptr なら「触れていない」扱い。
			 * ITouchBackend を直に叩かないのは、イベント駆動のラッチが取得で
			 * 消費されるため。毎フレーム 1 回だけ取得した状態を共有して読む
			 * (VirtualPadBackend と同じ流儀)。
			 * 渡されるのは仮想パッドが使っていない指だけの集合なので、
			 * ここで指の取り合いを考える必要は無い。
			 */
			const TouchState* touch_;

			/** 直前のカーソル位置(指が無いフレームはこれを返し続ける) */
			float lastCursorX_;
			float lastCursorY_;

			/** 前フレームにカーソルとして使った指の id(INVALID_TOUCH_ID なら指が無かった) */
			int32_t lastTouchId_;


		public:
			explicit TouchMouseBackend(const TouchState* touch);


		public:
			// タッチはデバイスの初期化を要さない(取得済みの TouchState を読むだけ)。
			bool Initialize(aq::graphics::NativeWindowHandle /*window*/) override { return true; }

			void Poll(MouseState& out) override;


		private:
			/** 指が無い状態。TouchPoint の未使用スロットと同じ値 */
			static constexpr int32_t INVALID_TOUCH_ID = -1;

			/**
			 * 左ボタンの添字。
			 * MouseState::buttons は MouseButton の値をそのまま添字に使う造りで、
			 * その先頭 MouseButton::Left が 0 に当たる。MouseButton は上位の判定用に
			 * Input.h が持つ enum で、バックエンドから参照すると依存が逆流するため、
			 * ここでは 0 を直に書く。
			 */
			static constexpr uint32_t LEFT_BUTTON_INDEX = 0;

			/** カーソルにする指(配列の先頭で id が有効なもの)。無ければ nullptr */
			static const TouchPoint* FindPointerTouch(const TouchState& touches);
		};
	}
}
