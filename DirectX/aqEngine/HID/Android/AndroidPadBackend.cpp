#include "aq.h"
// Android 以外では空 TU。
#if defined(AQ_PLATFORM_ANDROID)
#include "HID/Android/AndroidPadBackend.h"
#include "HID/Android/AndroidInputSink.h"


namespace aq
{
	namespace hid
	{
		void AndroidPadBackend::Poll(uint32_t index, PadState& out)
		{
			out = {};
			if (index != 0) {
				return;
			}

			AndroidInputSink::Get().FetchPad(out);
		}


		void AndroidPadBackend::SetVibration(uint32_t /*index*/, float /*left*/, float /*right*/)
		{
			// 振動は JNI(Vibrator / VibratorManager)が必要で NDK 単体では叩けない。
			// 入力取り込みの範囲外なので当面 no-op。
		}
	}
}
#endif // AQ_PLATFORM_ANDROID
