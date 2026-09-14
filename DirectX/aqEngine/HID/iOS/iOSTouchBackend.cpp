#include "aq.h"
// iOS 以外では空 TU。
#if defined(AQ_PLATFORM_IOS)
#include "HID/iOS/iOSTouchBackend.h"
#include "HID/iOS/iOSInputSink.h"


namespace aq
{
	namespace hid
	{
		void iOSTouchBackend::Poll(TouchState& out)
		{
			iOSInputSink::Get().FetchTouch(out);
		}
	}
}
#endif // AQ_PLATFORM_IOS
