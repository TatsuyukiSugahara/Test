#include "aq.h"
// macOS 以外では空 TU。
#if defined(AQ_PLATFORM_MAC)
#include "HID/Mac/CocoaKeyboardBackend.h"
#include "HID/Mac/CocoaInputSink.h"


namespace aq
{
	namespace hid
	{
		bool CocoaKeyboardBackend::Initialize(aq::graphics::NativeWindowHandle /*window*/)
		{
			// Cocoa ではイベント配送を NSApplication が行うため、開くデバイスが無い。
			// 協調レベルの設定(DirectInput の SetCooperativeLevel 相当)も不要。
			return true;
		}


		void CocoaKeyboardBackend::Poll(KeyboardState& out)
		{
			CocoaInputSink::Get().FetchKeyboard(out);
		}
	}
}
#endif // AQ_PLATFORM_MAC
