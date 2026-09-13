#include "aq.h"
#include "HID/TouchMouseBackend.h"


namespace aq
{
	namespace hid
	{
		namespace
		{
			/** 押下ビット。DirectInput のバッファ形式に合わせる(MouseState の約束) */
			static constexpr uint8_t PRESSED_BIT = 0x80;


			/** TouchState::count を配列長で丸める(バックエンドの値をそのまま信用しない) */
			inline uint32_t ClampTouchCount(const uint32_t count)
			{
				return count < TouchState::MAX_POINT_COUNT ? count : TouchState::MAX_POINT_COUNT;
			}
		}


		TouchMouseBackend::TouchMouseBackend(const TouchState* touch)
			: touch_(touch)
			, lastCursorX_(0.0f)
			, lastCursorY_(0.0f)
			, lastTouchId_(INVALID_TOUCH_ID)
		{
		}


		void TouchMouseBackend::Poll(MouseState& out)
		{
			out = {};

			// ホイールに当たる操作がタッチには無いので常に 0(out = {} のまま)。

			const TouchPoint* point = (touch_ != nullptr) ? FindPointerTouch(*touch_) : nullptr;
			if (point == nullptr) {
				// 指が無くてもカーソルは直前の位置に留める。マウスと違い「離した後も
				// そこにカーソルがある」概念がタッチには無いが、0 へ戻すと離した瞬間に
				// UI のホバーが外れ、押下 → 離すで成立するクリック判定が取れなくなる。
				out.cursorX  = lastCursorX_;
				out.cursorY  = lastCursorY_;
				lastTouchId_ = INVALID_TOUCH_ID;
				return;
			}

			// 触れている間だけ左ボタン押下。離した瞬間のフレーム(pressed = false)は
			// 座標だけを更新し、ボタンは 0 のままにして離上として見せる。
			if (point->pressed) {
				out.buttons[LEFT_BUTTON_INDEX] = PRESSED_BIT;
			}

			out.cursorX = point->x;
			out.cursorY = point->y;

			// 移動量は同じ指が続いているときだけ意味を持つ。指が変わった / 離れていた後の
			// 再接地では画面のどこへでも飛ぶので、0 にして視点などが跳ねるのを防ぐ。
			if (point->id == lastTouchId_) {
				out.dx = static_cast<int32_t>(point->x - lastCursorX_);
				out.dy = static_cast<int32_t>(point->y - lastCursorY_);
			}

			lastCursorX_ = point->x;
			lastCursorY_ = point->y;
			lastTouchId_ = point->id;
		}


		const TouchPoint* TouchMouseBackend::FindPointerTouch(const TouchState& touches)
		{
			// カーソルにするのは 1 本目の指だけ。2 本目以降はマルチタッチの
			// ジェスチャ用で、ポインタとしては捨てる。
			const uint32_t count = ClampTouchCount(touches.count);
			for (uint32_t i = 0; i < count; ++i) {
				if (touches.points[i].id == INVALID_TOUCH_ID) { continue; }
				return &touches.points[i];
			}
			return nullptr;
		}
	}
}
