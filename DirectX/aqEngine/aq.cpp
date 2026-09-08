#include "aq.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace aq
{
	namespace
	{
		// プロセス起動(静的初期化)時点を基準にする。WinMain より前に走るので「起動からの経過」に近い。
		const auto g_startupT0 = std::chrono::steady_clock::now();
	}


	void StartupMark(const char* label)
	{
		static std::mutex mtx;
		static double lastMs = 0.0;
		static FILE* fp = nullptr;
		static bool opened = false;

		const double ms = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - g_startupT0).count();

		std::lock_guard<std::mutex> lk(mtx);
		if (!opened)
		{
			// カレントディレクトリ(VS 既定は Game/)に毎回上書きで作る。
			opened = true;
			fopen_s(&fp, "startup_timing.log", "w");
		}

		char line[512];
		snprintf(line, sizeof(line), "[startup] %9.1f ms  (+%8.1f)  %s\n", ms, ms - lastMs, label ? label : "");
		lastMs = ms;

		if (fp) { fputs(line, fp); fflush(fp); }
#if defined(_WIN32)
		OutputDebugStringA(line);
#endif
	}


	void StartupMarkf(const char* fmt, ...)
	{
		char buf[480];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt ? fmt : "", args);
		va_end(args);
		StartupMark(buf);
	}
}
