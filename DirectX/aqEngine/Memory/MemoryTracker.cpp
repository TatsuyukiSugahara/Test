#include "aq.h"
#ifdef _DEBUG

#include "MemoryTracker.h"
#include <cstdio>
#include <atomic>
#include <windows.h>


namespace aq
{
	namespace memory
	{
		struct AllocationInfo
		{
			size_t      size;
			const char* file; // nullptr = ソース情報なし (通常の new)
			int         line;
			const char* func;
		};

		// スレッドローカル: engineNewWith マクロが次の Allocate 前にセットするソース情報
		struct AllocSource
		{
			const char* file = nullptr;
			int         line = 0;
			const char* func = nullptr;
		};
		thread_local AllocSource g_nextSource;

		// 再帰防止フラグ: TrackingData の map 操作が内部で new を呼ぶ際の無限再帰を防ぐ
		thread_local bool g_inTracking = false;

		// メモリ予算の観測用: 現在未解放のヒープ確保バイト数の総和。
		std::atomic<size_t> g_liveBytes{ 0 };


		struct TrackingData
		{
			std::mutex                                mutex;
			std::unordered_map<void*, AllocationInfo> map;
		};

		static TrackingData& GetData()
		{
			// リーキーシングルトン: main() 後の静的デストラクタから呼ばれても map が有効なまま保たれる
			static TrackingData* s_data = new TrackingData();
			return *s_data;
		}


		void SetNextAllocSource(const char* file, int line, const char* func) noexcept
		{
			g_nextSource = { file, line, func };
		}


		void TrackAllocation(void* ptr, size_t size) noexcept
		{
			if (!ptr || g_inTracking) {
				return;
			}

			// ソース情報を消費してリセット
			AllocSource src = g_nextSource;
			g_nextSource    = {};

			g_inTracking = true;
			{
				auto& data = GetData();
				std::lock_guard<std::mutex> lock(data.mutex);
				data.map[ptr] = AllocationInfo{ size, src.file, src.line, src.func };
				g_liveBytes.fetch_add(size, std::memory_order_relaxed);
			}
			g_inTracking = false;
		}


		void UntrackAllocation(void* ptr) noexcept
		{
			if (!ptr || g_inTracking) {
				return;
			}

			g_inTracking = true;
			{
				auto& data = GetData();
				std::lock_guard<std::mutex> lock(data.mutex);
				auto it = data.map.find(ptr);
				if (it != data.map.end()) {
					g_liveBytes.fetch_sub(it->second.size, std::memory_order_relaxed);
					data.map.erase(it);
				}
			}
			g_inTracking = false;
		}


		size_t GetTrackedBytes() noexcept
		{
			return g_liveBytes.load(std::memory_order_relaxed);
		}


		void ReportLeaks() noexcept
		{
			auto& data = GetData();
			std::lock_guard<std::mutex> lock(data.mutex);

			if (data.map.empty()) {
				OutputDebugStringA("[MemoryTracker] No leaks detected.\n");
				return;
			}

			char buf[512];
			snprintf(buf, sizeof(buf),
				"[MemoryTracker] ========== %zu leak(s) detected ==========\n",
				data.map.size());
			OutputDebugStringA(buf);

			size_t totalBytes = 0;
			for (const auto& [ptr, info] : data.map) {
				totalBytes += info.size;
				if (info.file) {
					snprintf(buf, sizeof(buf),
						"  %p  %6zu bytes  %s:%d  (%s)\n",
						ptr, info.size, info.file, info.line, info.func ? info.func : "");
				} else {
					snprintf(buf, sizeof(buf),
						"  %p  %6zu bytes  (no source info -- use engineNewWith for tracking)\n",
						ptr, info.size);
				}
				OutputDebugStringA(buf);
			}

			snprintf(buf, sizeof(buf),
				"[MemoryTracker] Total leaked: %zu bytes\n"
				"[MemoryTracker] =============================================\n",
				totalBytes);
			OutputDebugStringA(buf);
		}
	}
}

#endif // _DEBUG
