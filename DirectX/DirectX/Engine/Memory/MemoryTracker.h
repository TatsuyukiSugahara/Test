#pragma once

#ifdef _DEBUG
#include <cstddef>


namespace aq
{
	namespace memory
	{
		// アロケーションを記録する
		void TrackAllocation  (void* ptr, size_t size) noexcept;
		void UntrackAllocation(void* ptr)              noexcept;

		// プログラム終了時のリーク出力 (OutputDebugStringA)
		void ReportLeaks() noexcept;

		// engineNewWith マクロから呼ばれる: 次の Allocate に紐付けるソース情報をスレッドローカルにセット
		void SetNextAllocSource(const char* file, int line, const char* func) noexcept;
	}
}

#endif // _DEBUG
