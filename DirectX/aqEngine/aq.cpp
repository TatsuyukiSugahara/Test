#include "aq.h"
#include "Platform/Common/DebugOutput.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

// クラッシュスタックロガーはデスクトップ専用。dbghelp(StackWalk64/SymFromAddr)は
// WINAPI_PARTITION_DESKTOP のみで、UWP(AppContainer)では使えない。
// _WIN32 は UWP でも定義されるため、必ず AQ_PLATFORM_WIN32 で判定すること。
#if defined(AQ_PLATFORM_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

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

		const double ms = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - g_startupT0).count();

		std::lock_guard<std::mutex> lk(mtx);

		char line[512];
		snprintf(line, sizeof(line), "[startup] %9.1f ms  (+%8.1f)  %s", ms, ms - lastMs, label ? label : "");
		lastMs = ms;

#if defined(AQ_PLATFORM_UWP)
		// UWP は CWD がパッケージの読み取り専用フォルダで、ここにファイルを作れない
		// (作れないまま黙って捨てられ、実機で何も分からなくなる)。書き込める
		// LocalState へ回す。Xbox 実機はデバッガを繋げないのでこのログが頼りになる。
		StartupLog(line);
#elif defined(AQ_PLATFORM_ANDROID)
		// Android はカレントディレクトリ("/")へ書けず、標準出力/標準エラーも既定では
		// どこにも出ない。logcat が唯一の到達先なので DebugOutput へ流す
		// (`adb logcat -s AquaDash` で見える)。
		aq::debug::OutputString(line);
#else
		static FILE* fp = nullptr;
		static bool  opened = false;
		if (!opened)
		{
			// カレントディレクトリ(VS 既定は Game/)に毎回上書きで作る。
			opened = true;
			fp = fopen("startup_timing.log", "w");
		}
		if (fp) { fputs(line, fp); fputc('\n', fp); fflush(fp); }
#endif
#if defined(_WIN32)
		OutputDebugStringA(line);
		OutputDebugStringA("\n");
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


#if !defined(AQ_PLATFORM_UWP)
	// UWP 以外の StartupLog は「StartupMark へ流すだけ」で同一。
	// 以前は PlatformWin32.cpp / PlatformMac.mm が同じ一行をそれぞれ持っていたが、
	// プラットフォームを増やすたびに同じ定義が要るため既定実装をここへ置く。
	// UWP だけはパッケージの LocalState へ書く必要があるので PlatformUWP.cpp が持つ。
	void StartupLog(const char* msg)
	{
		StartupMark(msg);
	}
#endif


#if defined(AQ_PLATFORM_WIN32)
	namespace
	{
		/**
		 * 致命例外が起きた地点のコールスタックを startup_timing.log へ書き出す診断ハンドラ。
		 *
		 * このPCには cdb/WinDbg が無く、デバッガ無しで落ちた時に手掛かりが
		 * 「ログがどこで途切れたか」しか残らないため、例外アドレスと関数名/行番号を残す。
		 * 記録するだけで挙動は変えない(EXCEPTION_CONTINUE_SEARCH で通常のクラッシュ処理へ流す)。
		 *
		 * VEH は first-chance の全例外で呼ばれるので、C++ 例外(0xE06D7363)などは
		 * switch で即座に弾く。多重記録を避けるため記録は最初の 1 件だけ。
		 * スタックオーバーフローはスタックが尽きているため best-effort(出ないことがある)。
		 */
		LONG CALLBACK CrashStackLogger(EXCEPTION_POINTERS* info)
		{
			if (!info || !info->ExceptionRecord || !info->ContextRecord) {
				return EXCEPTION_CONTINUE_SEARCH;
			}

			const DWORD code = info->ExceptionRecord->ExceptionCode;
			switch (code)
			{
			case EXCEPTION_ACCESS_VIOLATION:
			case EXCEPTION_ILLEGAL_INSTRUCTION:
			case EXCEPTION_STACK_OVERFLOW:
			case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			case EXCEPTION_INT_DIVIDE_BY_ZERO:
			case EXCEPTION_PRIV_INSTRUCTION:
				break;
			default:
				return EXCEPTION_CONTINUE_SEARCH;
			}

			static LONG logged = 0;
			if (InterlockedCompareExchange(&logged, 1, 0) != 0) {
				return EXCEPTION_CONTINUE_SEARCH;
			}

			StartupMarkf("[crash] exception 0x%08lX at %p (thread %lu)",
				static_cast<unsigned long>(code),
				info->ExceptionRecord->ExceptionAddress,
				GetCurrentThreadId());

			// AV は「読み/書き/実行のどれで」「どのアドレスを」触ったかが原因究明の要になる。
			if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
			{
				const ULONG_PTR op   = info->ExceptionRecord->ExceptionInformation[0];
				const ULONG_PTR addr = info->ExceptionRecord->ExceptionInformation[1];
				StartupMarkf("[crash]   access violation %s address %p",
					op == 0 ? "reading" : (op == 1 ? "writing" : "executing"),
					reinterpret_cast<void*>(addr));
			}

			HANDLE process = GetCurrentProcess();
			HANDLE thread  = GetCurrentThread();

			SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
			SymInitialize(process, nullptr, TRUE);

			// 例外発生時のコンテキストから巻き戻す(ハンドラ自身のフレームではなく落ちた地点が出る)。
			CONTEXT      ctx   = *info->ContextRecord;
			STACKFRAME64 frame = {};
			DWORD        machine;
#if defined(_M_X64)
			machine                = IMAGE_FILE_MACHINE_AMD64;
			frame.AddrPC.Offset    = ctx.Rip;
			frame.AddrFrame.Offset = ctx.Rbp;
			frame.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_ARM64)
			machine                = IMAGE_FILE_MACHINE_ARM64;
			frame.AddrPC.Offset    = ctx.Pc;
			frame.AddrFrame.Offset = ctx.Fp;
			frame.AddrStack.Offset = ctx.Sp;
#else
			machine                = IMAGE_FILE_MACHINE_I386;
			frame.AddrPC.Offset    = ctx.Eip;
			frame.AddrFrame.Offset = ctx.Ebp;
			frame.AddrStack.Offset = ctx.Esp;
#endif
			frame.AddrPC.Mode    = AddrModeFlat;
			frame.AddrFrame.Mode = AddrModeFlat;
			frame.AddrStack.Mode = AddrModeFlat;

			alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512] = {};
			SYMBOL_INFO* sym  = reinterpret_cast<SYMBOL_INFO*>(symBuf);
			sym->SizeOfStruct = sizeof(SYMBOL_INFO);
			sym->MaxNameLen   = 500;

			for (int i = 0; i < 48; ++i)
			{
				if (!StackWalk64(machine, process, thread, &frame, &ctx,
						nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
					break;
				}
				const DWORD64 pc = frame.AddrPC.Offset;
				if (pc == 0) { break; }

				char    line[720];
				DWORD64 disp = 0;
				if (SymFromAddr(process, pc, &disp, sym))
				{
					IMAGEHLP_LINE64 src = {};
					src.SizeOfStruct = sizeof(src);
					DWORD srcDisp = 0;
					if (SymGetLineFromAddr64(process, pc, &srcDisp, &src)) {
						snprintf(line, sizeof(line), "[crash]   #%02d %s + 0x%llX  (%s:%lu)",
							i, sym->Name, static_cast<unsigned long long>(disp),
							src.FileName ? src.FileName : "?", src.LineNumber);
					} else {
						snprintf(line, sizeof(line), "[crash]   #%02d %s + 0x%llX",
							i, sym->Name, static_cast<unsigned long long>(disp));
					}
				}
				else
				{
					snprintf(line, sizeof(line), "[crash]   #%02d 0x%llX (no symbol)",
						i, static_cast<unsigned long long>(pc));
				}
				StartupMark(line);
			}

			StartupMark("[crash] --- end of stack ---");
			return EXCEPTION_CONTINUE_SEARCH;
		}


		// WinMain より前(静的初期化)に仕込んで、エンジン初期化中のクラッシュも拾えるようにする。
		struct CrashStackLoggerInstaller
		{
			CrashStackLoggerInstaller()
			{
				AddVectoredExceptionHandler(1 /*先頭に登録*/, CrashStackLogger);
			}
		};
		CrashStackLoggerInstaller g_crashStackLoggerInstaller;
	}
#endif
}
