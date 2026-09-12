#pragma once
#include <cstdint>

namespace aq
{
	namespace hid
	{
		// 1 点のタッチ。座標系は Mouse の cursorX/Y と揃えて
		// 「クライアント領域の左上原点・ピクセル」とする。
		struct TouchPoint
		{
			// OS が割り当てる識別子。指を離すまで同じ値が続く。未使用スロットは -1。
			int32_t id = -1;
			float   x  = 0.0f;
			float   y  = 0.0f;
			// 触れている間 true。離した瞬間のフレームだけ false で 1 回返る。
			bool    pressed = false;
		};

		// 現在触れている点の集合。
		// マウスと違い「カーソルが常に 1 つある」わけではないので、点の数も一緒に返す。
		struct TouchState
		{
			// MAX_TOUCH_COUNT という名前は使えない。Windows SDK の winuser.h が
			// 同名のマクロ(256)を定義しており、マクロ展開でメンバ宣言が壊れる。
			static constexpr uint32_t MAX_POINT_COUNT = 10;

			uint32_t   count = 0;
			TouchPoint points[MAX_POINT_COUNT]{};
		};

		// タッチ入力のプラットフォーム抽象。
		// 生のイベント取得だけを隠蔽し、仮想スティックの当たり判定や UI のタップ判定は
		// 上位(VirtualPadBackend / UI 層)に残す。
		// 実装: AndroidTouchBackend(Android)、NullTouchBackend(タッチが無い環境)。
		// 将来 iOS も ITouchBackend の実装を足すだけで済む形にしてある。
		class ITouchBackend
		{
		public:
			virtual ~ITouchBackend() = default;

			// タッチ状態を out に書き込む(触れていなければ count = 0)。
			virtual void Poll(TouchState& out) = 0;
		};
	}
}
