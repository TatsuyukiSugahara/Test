#include "aq.h"
// Android 以外では空 TU。
#if defined(AQ_PLATFORM_ANDROID)
#include "HID/Android/AndroidTouchBackend.h"
#include "HID/Android/AndroidInputSink.h"


namespace aq
{
	namespace hid
	{
		void AndroidTouchBackend::Poll(TouchState& out)
		{
			AndroidInputSink::Get().FetchTouch(out);
		}
	}
}
#endif // AQ_PLATFORM_ANDROID
