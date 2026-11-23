#pragma once


namespace aq
{
	namespace debug
	{
		/** デバッグ出力 (Win32/UWP = OutputDebugStringA, Mac = stderr) */
		void OutputString(const char* str);


		/**
		 * ログ 1 回分の文字列を受け取る関数。どのスレッドからも呼ばれる。
		 * 中でログを出してはいけない(StartupMark のロックの中で呼ぶため)
		 */
		using LogListener = void (*)(const char* text);

		/** 受け取る関数を 1 つ登録する(nullptr で外す)。登録は全プラットフォームで同じ(実装は aq.cpp) */
		void SetLogListener(LogListener listener);

		/** 受け取る関数が登録されていれば渡す(EnginePrintf と StartupMark が呼ぶ) */
		void NotifyLogListener(const char* text);
	}
}
