#include "aq.h"
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
#include "Platform/Common/DebugOutput.h"


namespace aq
{
	namespace debug
	{
		void OutputString(const char* str)
		{
			OutputDebugStringA(str);
		}
	}
}

#endif // AQ_PLATFORM_WINDOWS_FAMILY
