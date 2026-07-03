#pragma once

#ifdef _DEBUG
#include <cstddef>
#include <vector>


namespace aq
{
	namespace memory
	{
		// アロケーションを記録する
		void TrackAllocation  (void* ptr, size_t size) noexcept;
		void UntrackAllocation(void* ptr)              noexcept;

		// 現在トラッキング中(未解放)のヒープ確保バイト数の総和。メモリ予算の観測に使う。
		size_t GetTrackedBytes() noexcept;

		// 未解放アロケーションの件数。
		size_t GetTrackedCount() noexcept;

		// 確保サイト(file:line/func)ごとの未解放メモリ集計エントリ。
		struct MemoryUsageEntry
		{
			const char* file  = nullptr;  // nullptr = ソース情報なし(engineNewWith 未使用)
			const char* func  = nullptr;
			int         line  = 0;
			size_t      bytes = 0;
			size_t      count = 0;
		};

		// 現在未解放のアロケーションを確保サイトごとに集計して out に返す(bytes 未ソート)。
		void CaptureUsageBySource(std::vector<MemoryUsageEntry>& out) noexcept;

		// プログラム終了時のリーク出力 (OutputDebugStringA)
		void ReportLeaks() noexcept;

		// engineNewWith マクロから呼ばれる: 次の Allocate に紐付けるソース情報をスレッドローカルにセット
		void SetNextAllocSource(const char* file, int line, const char* func) noexcept;
	}
}

#endif // _DEBUG
