/**
 * 汎用処理群
 */
#pragma once


// アサート
#ifdef _DEBUG
#define EngineAssert(expr) if(!(expr)) { assert(expr); }
#define EngineAssertMsg(expr, message) if(!(expr)) { _wassert(_T(message), _CRT_WIDE(__FILE__), (unsigned)(__LINE__)); }
#else
#define EngineAssert(expression) ((void)0)
#define EngineAssertMsg(expr, message) ((void)0)
#endif


#define ArraySize(ary) (sizeof(ary) / sizeof(ary[0]))


namespace engine
{
	namespace memory
	{
		inline void Clear(void* ptr, uint32_t length)
		{
			ZeroMemory(ptr, length);
		}

		inline void Copy(void* dist, void* src, uint32_t size)
		{
			memcpy(dist, src, size);
		}
	}

	namespace util
	{
		template <typename T>
		using Function = std::function<T>;
	}

	namespace _internal
	{
		//void DebugPrintf(const char* format, ...)
		//{
		//	wchar_t _format[512];
		//	size_t ret;
		//	mbstowcs_s(&ret, _format, format, ArraySize(_format));

		//	// これくらいあれば足りるだろう
		//	TCHAR c[512];
		//	va_list argList;
		//	__crt_va_start(argList, _format);
		//	_stprintf_s(c, _format, argList);
		//	__crt_va_end(argList);
		//	OutputDebugString(c);
		//}
	}
}