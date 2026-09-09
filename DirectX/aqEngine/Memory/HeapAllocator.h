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
#ifdef _DEBUG
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
#ifdef _DEBUG
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
