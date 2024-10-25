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




// 配列数
#define ArraySize(ary) (sizeof(ary) / sizeof(ary[0]))




// デバッグ出力
#ifdef _DEBUG
#define EnginePrintf( fmt , ... ) engine::debug::Printf(fmt, __VA_ARGS__ )
#else
#define EnginePrintf( fmt , ... ) ((void)0)
#endif




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

	namespace debug
	{
		inline void Printf(const char* format, ...)
		{
			char temp[1024];	// これくらいあれば足りるだろう
			va_list ap;
			
			va_start(ap, format);

			vsprintf_s(temp, format, ap);
			OutputDebugStringA(temp);

			va_end(ap);
		}
	}
}