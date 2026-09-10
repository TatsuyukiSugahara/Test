#pragma once
#include "HID/IKeyboardBackend.h"

namespace aq
{
	namespace hid
	{
		// 入力の無いキーボードバックエンド(常に全ゼロ)。
		// UWP と、Cocoa 実装が入るまでの Mac(P2〜P3)で使う。
		class NullKeyboardBackend : public IKeyboardBackend
		{
		public:
			bool Initialize(aq::graphics::NativeWindowHandle /*window*/) override { return true; }
			void Poll(KeyboardState& out) override { out = {}; }
		};
	}
}
