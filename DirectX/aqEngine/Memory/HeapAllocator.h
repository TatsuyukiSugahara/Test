#pragma once
#include "IAllocator.h"
#include "Platform/Common/AlignedAlloc.h"
#include <cassert>
#include <cstdint>


namespace aq
{
	namespace memory
	{
		/**
		 * システムヒープを使うアロケータ。
		 * aq::memory::AlignedAlloc / AlignedFree は CRT レベルでスレッドセーフ。
		 *
		 * デバッグビルドでは各ブロックのユーザー領域直前に TrackHeader を埋め込み、MemoryTracker の
		 * ライブリストに連結する(確保ごとの二次確保なし)。レイアウト:
		 *   [ AlignedAlloc 先頭 ][ 詰め物 ][ TrackHeader ][ ユーザー領域(alignment 境界) ]
		 *   headerSize = sizeof(TrackHeader) を alignment の倍数に切り上げた値。
		 *
		 * ★ **iOS では Debug でもヘッダを付けない。**
		 *
		 * グローバル operator new を差し替えている(Memory/GlobalNew.cpp)関係で、
		 * **システムフレームワークの確保もこのアロケータを通る**(Apple の dyld は
		 * フラット名前空間なので実行ファイル側の定義が先に解決される)。
		 * ヘッダを付けると返すポインタが実確保の先頭からずれるため、
		 * **相手が free() で解放した瞬間に壊れる**。逆方向(他所のポインタが
		 * こちらへ来る)は magic で弾けるが、この方向は弾きようがない。
		 *
		 * iOS 実機(iPhone 17 / iOS 26.6.2)で、AudioToolbox が AudioUnitInitialize の
		 * 中で SIGBUS(BUS_ADRALN)を起こして再現した。Release(ヘッダ無し)では
		 * 起きず、Debug でも GlobalNew.cpp を外すと起きないことまで切り分けてある。
		 * 設計書/iOS移植設計.md §6。
		 *
		 * **代償はリーク検出が iOS で効かなくなること。**ただし iOS は
		 * applicationWillTerminate: が走る保証が無く(§3.2)、**元からリーク報告を
		 * 品質ゲートに使えない**ので実害は小さい。
		 * 恒久対処(エンジン型だけを差し替え対象にする等)は移植とは別に切る。
		 */
		class HeapAllocator : public IAllocator
		{
		public:
			void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override
			{
				if (size == 0) {
					return nullptr;
				}
				if (alignment < sizeof(void*)) {
					alignment = sizeof(void*);
				}
#if defined(_DEBUG) && !defined(AQ_PLATFORM_IOS)
				const size_t headerSize = (sizeof(TrackHeader) + alignment - 1) / alignment * alignment;
				uint8_t* raw = static_cast<uint8_t*>(AlignedAlloc(headerSize + size, alignment));
				assert(raw && "HeapAllocator: allocation failed");
				if (!raw) {
					return nullptr;
				}
				uint8_t* user = raw + headerSize;
				auto* header = reinterpret_cast<TrackHeader*>(user - sizeof(TrackHeader));
				header->headerOffset = static_cast<uint32_t>(headerSize);
				RegisterBlock(header, size);
				return user;
#else
				void* ptr = AlignedAlloc(size, alignment);
				assert(ptr && "HeapAllocator: allocation failed");
				return ptr;
#endif
			}

			void Deallocate(void* ptr) noexcept override
			{
				if (!ptr) {
					return;
				}
#if defined(_DEBUG) && !defined(AQ_PLATFORM_IOS)
				auto* user   = static_cast<uint8_t*>(ptr);
				auto* header = reinterpret_cast<TrackHeader*>(user - sizeof(TrackHeader));
				if (header->magic == TRACK_MAGIC) {
					const uint32_t headerOffset = header->headerOffset;
					UnregisterBlock(header);
					AlignedFree(user - headerOffset);
				}
				else {
					// 本アロケータ以外(生の AlignedAlloc 等)から来たポインタ、または二重解放。
					assert(header->magic != 0 && "HeapAllocator: double free detected");
					AlignedFree(ptr);
				}
#else
				AlignedFree(ptr);
#endif
			}
		};
	}
}
