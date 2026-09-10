#pragma once


namespace aq
{
	namespace debug
	{
		/** デバッグ出力 (Win32/UWP = OutputDebugStringA, Mac = stderr) */
		void OutputString(const char* str);
	}
}
