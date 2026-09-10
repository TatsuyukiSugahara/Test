#pragma once
#include "HID/IMouseBackend.h"

namespace aq
{
	namespace hid
	{
		// 入力の無いマウスバックエンド(常に全ゼロ)。
		// UWP と、Cocoa 実装が入るまでの Mac(P2〜P3)で使う。
		class NullMouseBackend : public IMouseBackend
		{
		public:
			bool Initialize(aq::graphics::NativeWindowHandle /*window*/) override { return true; }
			void Poll(MouseState& out) override { out = {}; }
		};
	}
}
