/**
 * 汎用処理群
 */
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include "Platform/Common/DebugOutput.h"


// アサート
#ifdef _DEBUG
#define EngineAssert(expr) if(!(expr)) { assert(expr); }
#define EngineAssertMsg(expr, message) if(!(expr)) { aq::debug::OutputString("Assertion failed: " message "\n"); assert(expr); }
#else
#define EngineAssert(expression) ((void)0)
#define EngineAssertMsg(expr, message) ((void)0)
#endif




// 配列数
#define ArraySize(ary) (sizeof(ary) / sizeof(ary[0]))




// デバッグ出力
//
// 可変引数を fmt と __VA_ARGS__ に分けて書くと、引数が書式文字列だけのときに
// 末尾のカンマが残る。MSVC / clang-cl は独自拡張で黙って落としてくれるが、
// 標準準拠モードの clang(Mac ビルド)は "expected expression" で落ちる。
// 分けずに丸ごと転送すればカンマ自体が発生しない(__VA_OPT__ は MSVC の
// 従来プリプロセッサが未対応なので使わない)。
#ifdef _DEBUG
#define EnginePrintf( ... ) aq::debug::Printf( __VA_ARGS__ )
#else
#define EnginePrintf( ... ) ((void)0)
#endif




namespace aq
{
	namespace memory
	{
		inline void Clear(void* ptr, uint32_t length)
		{
			memset(ptr, 0, length);
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

			vsnprintf(temp, sizeof(temp), format, ap);
			aq::debug::OutputString(temp);

			va_end(ap);
		}
	}
}