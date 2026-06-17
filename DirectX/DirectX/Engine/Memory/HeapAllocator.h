#pragma once
#include "IAllocator.h"
#include <malloc.h>
#include <cassert>


namespace aq
{
	namespace memory
	{
		/**
		 * システムヒープを使うアロケータ。
		 * _aligned_malloc / _aligned_free は CRT レベルでスレッドセーフ。
		 * デバッグビルドでは MemoryTracker に確保・解放を通知する。
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
				void* ptr = _aligned_malloc(size, alignment);
				assert(ptr && "HeapAllocator: allocation failed");

#ifdef _DEBUG
				TrackAllocation(ptr, size);
#endif
				return ptr;
			}

			void Deallocate(void* ptr) noexcept override
			{
#ifdef _DEBUG
				UntrackAllocation(ptr);
#endif
				_aligned_free(ptr);
			}
		};
	}
}
