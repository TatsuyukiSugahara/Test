#include "aq.h"
// iOS 以外では空 TU。
#if defined(AQ_PLATFORM_IOS)
#include "HID/iOS/iOSInputSink.h"


namespace aq
{
	namespace hid
	{
		iOSInputSink& iOSInputSink::Get()
		{
			static iOSInputSink instance;
			return instance;
		}


		void iOSInputSink::OnTouchBegan(const void* touchKey, const float x, const float y)
		{
			TouchSlot* slot = FindTouchSlot(touchKey, true);
			if (slot == nullptr) {
				// 同時に触れられる点数を超えた。後から触れた指は捨てる
				// (MAX_POINT_COUNT = 10 なので実機で起きるのは事故のときだけ)。
				return;
			}

			slot->touchKey          = touchKey;
			slot->x                 = x;
			slot->y                 = y;
			slot->down              = true;
			slot->pressedSinceFetch = true;
		}


		void iOSInputSink::OnTouchMoved(const void* touchKey, const float x, const float y)
		{
			TouchSlot* slot = FindTouchSlot(touchKey, false);
			if (slot == nullptr) {
				// 押下を取り逃していても動きは拾えるようにしておく
				// (アクティブ化直後など、began が来ないまま moved から始まる場合がある)。
				OnTouchBegan(touchKey, x, y);
				return;
			}

			slot->x = x;
			slot->y = y;
		}


		void iOSInputSink::OnTouchEnded(const void* touchKey, const float x, const float y)
		{
			TouchSlot* slot = FindTouchSlot(touchKey, false);
			if (slot == nullptr) {
				return;
			}

			// スロットは消さない。次の取得で pressed = false を 1 回返してから空ける
			// (上位のタップ判定は「押下 → 解放」の組で成立するため)。
			slot->x    = x;
			slot->y    = y;
			slot->down = false;

			// touchKey はここで捨てる。UITouch のオブジェクトは ended の直後に
			// システムへ返って別の指へ使い回されるので、残しておくと次に触れた指を
			// 「解放待ちの古いスロット」と誤って同定してしまう。
			slot->touchKey = nullptr;
		}


		void iOSInputSink::OnTouchCancelled()
		{
			for (TouchSlot& slot : touches_)
			{
				slot.down     = false;
				slot.touchKey = nullptr;

				// 取りこぼし対策の latch も落とす。OS にジェスチャを奪われた場合は
				// 「押して離した」ではないので、タップとして成立させてはいけない。
				slot.pressedSinceFetch = false;
			}
		}


		void iOSInputSink::FetchTouch(TouchState& out)
		{
			out = {};

			for (TouchSlot& slot : touches_)
			{
				if (slot.id < 0) {
					continue;
				}

				const bool pressed     = slot.down || slot.pressedSinceFetch;
				slot.pressedSinceFetch = false;

				if (out.count < TouchState::MAX_POINT_COUNT) {
					TouchPoint& point = out.points[out.count];
					point.id      = slot.id;
					point.x       = slot.x;
					point.y       = slot.y;
					point.pressed = pressed;
					++out.count;
				}

				// pressed = false を返した回で解放する。これで「離した瞬間のフレームだけ
				// pressed = false が 1 回返る」形になる。
				if (!pressed) {
					slot = TouchSlot{};
				}
			}
		}


		iOSInputSink::TouchSlot* iOSInputSink::FindTouchSlot(const void* touchKey, const bool create)
		{
			if (touchKey == nullptr) {
				return nullptr;
			}

			for (TouchSlot& slot : touches_)
			{
				if (slot.touchKey == touchKey) {
					return &slot;
				}
			}

			if (!create) {
				return nullptr;
			}

			for (uint32_t i = 0; i < TouchState::MAX_POINT_COUNT; ++i)
			{
				if (touches_[i].id < 0) {
					// id は採番せずスロットの添字をそのまま使う(ヘッダの touchKey / id の説明)。
					touches_[i].id = static_cast<int32_t>(i);
					return &touches_[i];
				}
			}
			return nullptr;
		}
	}
}
#endif // AQ_PLATFORM_IOS
