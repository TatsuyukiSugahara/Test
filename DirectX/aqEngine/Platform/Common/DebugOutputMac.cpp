#include "Platform/Common/PlatformDefs.h"
#if defined(AQ_PLATFORM_MAC)
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

#endif // AQ_PLATFORM_MAC
