#pragma once
#include <cstdint>
#include "Graphics/GraphicsTypes.h"   // NativeWindowHandle

namespace aq
{
	namespace hid
	{
		// マウス状態。DIMOUSESTATE2 の中立版で、フィールドの意味は次のとおり。
		//   dx / dy    : 相対移動量 (DIMOUSESTATE2::lX / lY)
		//   wheel      : ホイールの相対量 (DIMOUSESTATE2::lZ)
		//   buttons    : ボタン状態。押下中は 0x80 が立つ (DIMOUSESTATE2::rgbButtons)
		//   cursorX/Y  : クライアント座標系のカーソル位置 (左上原点)
		struct MouseState
		{
			static constexpr uint32_t BUTTON_COUNT = 8;

			int32_t dx     = 0;
			int32_t dy     = 0;
			int32_t wheel  = 0;
			uint8_t buttons[BUTTON_COUNT]{};   // 押下 = 0x80
			float   cursorX = 0.0f;
			float   cursorY = 0.0f;
		};

		// マウス入力のプラットフォーム抽象。
		// 生のデバイス取得だけを隠蔽し、トリガー/長押し等の判定ロジックは Mouse 側に残す。
		// 実装: DirectInputMouseBackend(Win32)、NullMouseBackend(UWP / Mac の P2〜P3)、
		//       将来 CocoaMouseBackend(Mac, P4)。
		class IMouseBackend
		{
		public:
			virtual ~IMouseBackend() = default;

			// デバイスを初期化する。window は協調レベル設定やカーソル座標の変換に使う。成功で true。
			virtual bool Initialize(aq::graphics::NativeWindowHandle window) = 0;

			// マウスをポーリングし、状態を out に書き込む(未取得なら全ゼロ)。
			virtual void Poll(MouseState& out) = 0;
		};
	}
}
