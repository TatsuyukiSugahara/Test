#include "aq.h"
#ifdef _DEBUG

#include "MemoryTracker.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <unordered_map>


namespace aq
{
	namespace memory
	{
		// スレッドローカル: engineNewWith マクロが次の Allocate 前にセットするソース情報
		struct AllocSource
		{
			const char* file = nullptr;
			int         line = 0;
			const char* func = nullptr;
		};
		thread_local AllocSource g_nextSource;

		// 再入防止フラグ: 集計(CaptureUsageBySource)中の一時確保がリストのロックへ再入しないようにする。
		// フラグが立っている間の確保はヘッダは持つがリストには連結しない(tracked=false)。
		thread_local bool g_inTracking = false;

		// メモリ予算の観測用: 現在未解放のヒープ確保バイト数の総和と件数。
		std::atomic<size_t> g_liveBytes{ 0 };
		std::atomic<size_t> g_liveCount{ 0 };


		// ライブブロックの侵入型双方向リスト。番兵ノードを持ち、連結/切断は O(1)。
		struct TrackingData
		{
			std::mutex  mutex;
			TrackHeader sentinel;

			TrackingData()
			{
				sentinel = {};
				sentinel.prev = &sentinel;
				sentinel.next = &sentinel;
			}
		};

		static TrackingData& GetData()
		{
			// リーキーシングルトン: main() 後の静的デストラクタから呼ばれても有効なまま保たれる。
			// 自身の確保は g_inTracking を立てて行い、リーク報告に混ざらないようにする。
			static TrackingData* s_data = []() {
				const bool prev = g_inTracking;
				g_inTracking = true;
				auto* d = new TrackingData();
				g_inTracking = prev;
				return d;
			}();
			return *s_data;
		}


		void SetNextAllocSource(const char* file, int line, const char* func) noexcept
		{
			g_nextSource = { file, line, func };
		}


		void RegisterBlock(TrackHeader* header, size_t size) noexcept
		{
			// ソース情報を消費してリセット
			const AllocSource src = g_nextSource;
			g_nextSource = {};

			header->magic   = TRACK_MAGIC;
			header->size    = size;
			header->file    = src.file;
			header->func    = src.func;
			header->line    = src.line;
			header->tracked = false;
			header->prev    = nullptr;
			header->next    = nullptr;

			if (g_inTracking) {
				return;   // 集計中の一時確保: 統計とリストには含めない
			}

			auto& data = GetData();
			std::lock_guard<std::mutex> lock(data.mutex);
			header->tracked = true;
			header->prev    = data.sentinel.prev;
			header->next    = &data.sentinel;
			data.sentinel.prev->next = header;
			data.sentinel.prev       = header;
			g_liveBytes.fetch_add(size, std::memory_order_relaxed);
			g_liveCount.fetch_add(1,    std::memory_order_relaxed);
		}


		void UnregisterBlock(TrackHeader* header) noexcept
		{
			if (header->tracked) {
				auto& data = GetData();
				std::lock_guard<std::mutex> lock(data.mutex);
				header->prev->next = header->next;
				header->next->prev = header->prev;
				g_liveBytes.fetch_sub(header->size, std::memory_order_relaxed);
				g_liveCount.fetch_sub(1,            std::memory_order_relaxed);
			}
			header->magic   = 0;   // 二重解放検出用
			header->tracked = false;
			header->prev    = nullptr;
			header->next    = nullptr;
		}


		size_t GetTrackedBytes() noexcept
		{
			return g_liveBytes.load(std::memory_order_relaxed);
		}


		size_t GetTrackedCount() noexcept
		{
			return g_liveCount.load(std::memory_order_relaxed);
		}


		void CaptureUsageBySource(std::vector<MemoryUsageEntry>& out) noexcept
		{
			out.clear();
			// 集計中の一時確保(agg / out)がリストへ連結・ロック再入しないようガードする。
			const bool prev = g_inTracking;
			g_inTracking = true;
			{
				auto& data = GetData();
				std::lock_guard<std::mutex> lock(data.mutex);

				// (file,line,func) をキーに集計(file/func はサイト固有の安定ポインタ)。
				struct Key { const char* file; int line; const char* func;
					bool operator==(const Key& o) const { return file == o.file && line == o.line && func == o.func; } };
				struct KeyHash { size_t operator()(const Key& k) const {
					size_t h = std::hash<const void*>()(k.file);
					h ^= std::hash<int>()(k.line) + 0x9e3779b9u + (h << 6) + (h >> 2);
					h ^= std::hash<const void*>()(k.func) + 0x9e3779b9u + (h << 6) + (h >> 2);
					return h; } };
				std::unordered_map<Key, size_t, KeyHash> agg;
				agg.reserve(g_liveCount.load(std::memory_order_relaxed));

				for (const TrackHeader* h = data.sentinel.next; h != &data.sentinel; h = h->next) {
					const Key key{ h->file, h->line, h->func };
					auto it = agg.find(key);
					if (it == agg.end()) {
						agg.emplace(key, out.size());
						out.push_back(MemoryUsageEntry{ h->file, h->func, h->line, h->size, 1 });
					} else {
						out[it->second].bytes += h->size;
						out[it->second].count += 1;
					}
				}
			}
			g_inTracking = prev;
		}


		void ReportLeaks() noexcept
		{
			auto& data = GetData();
			std::lock_guard<std::mutex> lock(data.mutex);

			if (data.sentinel.next == &data.sentinel) {
				aq::debug::OutputString("[MemoryTracker] No leaks detected.\n");
				return;
			}

			char buf[512];
			snprintf(buf, sizeof(buf),
				"[MemoryTracker] ========== %zu leak(s) detected ==========\n",
				g_liveCount.load(std::memory_order_relaxed));
			aq::debug::OutputString(buf);

			size_t totalBytes = 0;
			for (const TrackHeader* h = data.sentinel.next; h != &data.sentinel; h = h->next) {
				const void* userPtr = reinterpret_cast<const uint8_t*>(h) + sizeof(TrackHeader);
				totalBytes += h->size;
				if (h->file) {
					snprintf(buf, sizeof(buf),
						"  %p  %6zu bytes  %s:%d  (%s)\n",
						userPtr, h->size, h->file, h->line, h->func ? h->func : "");
				} else {
					snprintf(buf, sizeof(buf),
						"  %p  %6zu bytes  (no source info -- use engineNewWith for tracking)\n",
						userPtr, h->size);
				}
				aq::debug::OutputString(buf);
			}

			snprintf(buf, sizeof(buf),
				"[MemoryTracker] Total leaked: %zu bytes\n"
				"[MemoryTracker] =============================================\n",
				totalBytes);
			aq::debug::OutputString(buf);
		}
	}
}

#endif // _DEBUG
