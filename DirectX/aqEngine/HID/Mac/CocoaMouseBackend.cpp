#include "aq.h"
// macOS 以外では空 TU。
#if defined(AQ_PLATFORM_MAC)
#include "HID/Mac/CocoaMouseBackend.h"
#include "HID/Mac/CocoaInputSink.h"


namespace aq
{
	namespace hid
	{
		bool CocoaMouseBackend::Initialize(aq::graphics::NativeWindowHandle /*window*/)
		{
			// 開くデバイスが無い(理由は CocoaKeyboardBackend::Initialize と同じ)。
			return true;
		}


		void CocoaMouseBackend::Poll(MouseState& out)
		{
			CocoaInputSink::Get().FetchMouse(out);
		}
	}
}
#endif // AQ_PLATFORM_MAC
