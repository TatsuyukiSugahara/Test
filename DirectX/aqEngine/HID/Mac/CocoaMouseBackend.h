#pragma once
// macOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC)
#include "HID/IMouseBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * Cocoa のマウス入力。
		 *
		 * `CocoaKeyboardBackend` と同じく `CocoaInputSink` を読むだけ。
		 * カーソル位置のビュー座標への変換と Y 反転は、NSView を持っている
		 * `PlatformMac` 側で済ませてある(設計書/Mac移植設計.md §3.2)。
		 */
		class CocoaMouseBackend : public IMouseBackend
		{
		public:
			bool Initialize(aq::graphics::NativeWindowHandle window) override;
			void Poll(MouseState& out) override;
		};
	}
}
#endif // AQ_PLATFORM_MAC
