#include "Platform/Common/PlatformDefs.h"
#if defined(AQ_PLATFORM_ANDROID)
#include "Platform/Common/DebugOutput.h"
#include <android/log.h>


namespace aq
{
	namespace debug
	{
		namespace
		{
			// logcat のタグ。`adb logcat -s AquaDash` で絞り込める。
			constexpr const char* LOG_TAG = "AquaDash";
		}


		void OutputString(const char* str)
		{
			if (str == nullptr) {
				return;
			}

			// Mac の stderr と違い、Android はアプリの標準出力/標準エラーが
			// 既定でどこにも出ない。logcat が唯一の到達先になる。
			__android_log_write(ANDROID_LOG_INFO, LOG_TAG, str);
		}
	}
}

#endif // AQ_PLATFORM_ANDROID
