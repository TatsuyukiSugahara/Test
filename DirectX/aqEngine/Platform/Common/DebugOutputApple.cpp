#include "Platform/Common/PlatformDefs.h"
// Apple プラットフォーム(macOS / iOS)共通。stderr への出力はどちらでも同じに動く
// (iOS でも Xcode のコンソール / os_log のデバッグ出力へ流れる)。
#if defined(AQ_PLATFORM_APPLE)
#include "Platform/Common/DebugOutput.h"
#include <cstdio>


namespace aq
{
	namespace debug
	{
		void OutputString(const char* str)
		{
			fputs(str, stderr);
		}
	}
}

#endif // AQ_PLATFORM_APPLE
