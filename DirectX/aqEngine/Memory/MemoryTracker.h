#pragma once

#ifdef _DEBUG
#include <cstddef>
#include <cstdint>
#include <vector>


namespace aq
{
	namespace memory
	{
		/**
		 * 追跡ヘッダ。HeapAllocator(Debug)が各確保ブロックのユーザー領域直前に置く。
		 * 以前は「ポインタ → 情報」の unordered_map で追跡していたが、確保 1 回ごとに map ノードの
		 * 二次確保 + mutex + ハッシュ挿入が走り、Debug の確保コストの主因になっていた(JSON 解析で実測)。
		 * ヘッダ方式は追加確保なし・O(1) の侵入型双方向リストで、リーク報告/サイト別集計はリストを走査する。
		 */
		struct TrackHeader
		{
			uint32_t     magic;        // TRACK_MAGIC: 本アロケータのブロックである印。解放時に 0 にして二重解放を検出
			uint32_t     headerOffset; // ユーザー領域先頭 - _aligned_malloc が返した先頭(アラインメント調整込み)
			size_t       size;         // ユーザー要求サイズ
			const char*  file;         // nullptr = ソース情報なし(通常の new)
			const char*  func;
			int          line;
			bool         tracked;      // リストに連結したか(追跡中の再入時は連結しない)
			TrackHeader* prev;
			TrackHeader* next;
		};

		constexpr uint32_t TRACK_MAGIC = 0xA11C0DE5u;

		// ヘッダを初期化してライブリストに連結する(g_nextSource を消費)。
		void RegisterBlock(TrackHeader* header, size_t size) noexcept;
		// ライブリストから外す。magic を潰して二重解放を検出できるようにする。
		void UnregisterBlock(TrackHeader* header) noexcept;

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
