#pragma once
// macOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC)
#include "HID/IKeyboardBackend.h"

namespace aq
{
	namespace hid
	{
		/**
		 * Cocoa のキーボード入力。
		 *
		 * デバイスを直接開かず、`PlatformMac::PumpEvents` が `CocoaInputSink` へ流した
		 * NSEvent の結果を読むだけ(設計書/Mac移植設計.md §3.2)。
		 * そのため `Initialize` はウィンドウを見ておらず、常に成功する。
		 */
		class CocoaKeyboardBackend : public IKeyboardBackend
		{
		public:
			bool Initialize(aq::graphics::NativeWindowHandle window) override;
			void Poll(KeyboardState& out) override;
		};
	}
}
#endif // AQ_PLATFORM_MAC
