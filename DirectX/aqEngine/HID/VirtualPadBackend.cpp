#include "aq.h"
#include "HID/VirtualPadBackend.h"
#include <cmath>


namespace aq
{
	namespace hid
	{
		namespace
		{
			/** TouchState::count を配列長で丸める(バックエンドの値をそのまま信用しない) */
			inline uint32_t ClampTouchCount(const uint32_t count)
			{
				return count < TouchState::MAX_POINT_COUNT ? count : TouchState::MAX_POINT_COUNT;
			}
		}


		VirtualPadBackend::VirtualPadBackend(const TouchState* touch)
			: touch_(touch)
			, layout_()
			, stickTouchId_(INVALID_TOUCH_ID)
		{
		}


		void VirtualPadBackend::Poll(uint32_t index, PadState& out)
		{
			out = {};

			// タッチ画面は 1 枚しか無いので、仮想パッドは index 0 だけ。
			if (index != 0) { return; }

			// 常に接続扱い。false にするとゲーム側が「パッド無し」と判断して
			// 操作を受け付けなくなる。
			out.connected = true;

			// 入力元が無いときは触れていない扱い。掴みも解く。
			if (touch_ == nullptr) {
				stickTouchId_ = INVALID_TOUCH_ID;
				return;
			}

			// タッチ座標はクライアント左上原点のピクセルなので、正規化で持っている
			// レイアウトを毎フレームここでピクセルへ直す。こうしておけば解像度や
			// 画面回転が変わってもレイアウトが崩れない。
			const float screenWidth  = static_cast<float>(aq::Engine::Get().GetScreenWidth());
			const float screenHeight = static_cast<float>(aq::Engine::Get().GetScreenHeight());
			if (screenWidth <= 0.0f || screenHeight <= 0.0f) {
				stickTouchId_ = INVALID_TOUCH_ID;
				return;
			}

			UpdateStick  (*touch_, screenWidth, screenHeight, out);
			UpdateButtons(*touch_, screenWidth, screenHeight, out);
		}


		void VirtualPadBackend::UpdateStick(const TouchState& touches, const float screenWidth, const float screenHeight, PadState& out)
		{
			const Circle& area   = layout_.leftStick;
			const float   radius = area.radius * screenHeight;
			if (radius <= 0.0f) {
				stickTouchId_ = INVALID_TOUCH_ID;
				return;
			}

			// 掴んだ指は円の外へ出ても離すまで追従する。縁で掴みが外れると
			// 全開に倒したところで操作が途切れてしまうため。
			const TouchPoint* point = FindTouch(touches, stickTouchId_);
			if (point == nullptr) {
				stickTouchId_ = INVALID_TOUCH_ID;

				// 掴んでいなければ、円内に入った点を 1 つだけ掴む
				const uint32_t count = ClampTouchCount(touches.count);
				for (uint32_t i = 0; i < count; ++i) {
					const TouchPoint& candidate = touches.points[i];
					if (candidate.id == INVALID_TOUCH_ID || !candidate.pressed) { continue; }
					if (!IsInside(area, candidate, screenWidth, screenHeight)) { continue; }

					point         = &touches.points[i];
					stickTouchId_ = candidate.id;
					break;
				}
			}

			// 掴んでいない間は中心(軸はゼロのまま)
			if (point == nullptr) { return; }

			float x = (point->x - area.centerX * screenWidth)  / radius;
			float y = (point->y - area.centerY * screenHeight) / radius;

			// 円の外へ出た分は半径でクランプし、縁に張り付かせる
			const float lengthSq = x * x + y * y;
			if (lengthSq > 1.0f) {
				const float scale = 1.0f / std::sqrt(lengthSq);
				x *= scale;
				y *= scale;
			}

			// LY は XInputPadBackend / DualSensePadBackend と同じく上方向が +。
			// タッチ座標は下方向が + なので符号を反転する。
			out.axes[static_cast<uint32_t>(PadAxis::LX)] =  x;
			out.axes[static_cast<uint32_t>(PadAxis::LY)] = -y;
		}


		void VirtualPadBackend::UpdateButtons(const TouchState& touches, const float screenWidth, const float screenHeight, PadState& out) const
		{
			const uint32_t count = ClampTouchCount(touches.count);
			for (uint32_t i = 0; i < count; ++i) {
				const TouchPoint& point = touches.points[i];
				if (point.id == INVALID_TOUCH_ID || !point.pressed) { continue; }

				// スティックを掴んでいる指は円外まで追従するので、
				// 通りがかったボタンを押してしまわないようボタン判定からは除く。
				if (point.id == stickTouchId_) { continue; }

				if (IsInside(layout_.buttonA, point, screenWidth, screenHeight)) {
					out.buttons[static_cast<uint32_t>(PadButton::A)] = true;
				}
				if (IsInside(layout_.buttonB, point, screenWidth, screenHeight)) {
					out.buttons[static_cast<uint32_t>(PadButton::B)] = true;
				}
				if (IsInside(layout_.buttonStart, point, screenWidth, screenHeight)) {
					out.buttons[static_cast<uint32_t>(PadButton::Start)] = true;
				}
			}
		}


		bool VirtualPadBackend::IsInside(const Circle& circle, const TouchPoint& point, const float screenWidth, const float screenHeight)
		{
			const float radius = circle.radius * screenHeight;
			if (radius <= 0.0f) { return false; }

			const float x = point.x - circle.centerX * screenWidth;
			const float y = point.y - circle.centerY * screenHeight;
			return (x * x + y * y) <= (radius * radius);
		}


		const TouchPoint* VirtualPadBackend::FindTouch(const TouchState& touches, const int32_t id)
		{
			if (id == INVALID_TOUCH_ID) { return nullptr; }

			const uint32_t count = ClampTouchCount(touches.count);
			for (uint32_t i = 0; i < count; ++i) {
				// 離した瞬間のフレームは pressed = false で来る。そこで掴みを解く。
				if (touches.points[i].id == id && touches.points[i].pressed) {
					return &touches.points[i];
				}
			}
			return nullptr;
		}
	}
}
