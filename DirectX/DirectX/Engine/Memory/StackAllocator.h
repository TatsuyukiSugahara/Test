#pragma once
#include "IAllocator.h"
#include <cstdint>
#include <atomic>
#include <cassert>
#include <malloc.h>


namespace aq
{
	namespace memory
	{
		/**
		 * 線形スタックアロケータ。
		 *
		 * 構築時に指定サイズのバッファを一度だけ確保し、以降は解放しない。
		 * Allocate はオフセットを進めるだけで高速。
		 * Deallocate は何もしない。解放は ScopedMarker または Reset() で行う。
		 *
		 * スレッドセーフ: オフセットを atomic で CAS 更新する。
		 * ただし ScopedMarker / Reset はすべてのスレッドの Allocate が完了した後に使うこと。
		 */
		class StackAllocator : public IAllocator
		{
		public:
			using Marker = size_t;


			/**
			 * ローカル変数として宣言するだけで現在位置をマークし、
			 * スコープを抜けると自動的にその位置まで巻き戻す RAII マーカー。
			 *
			 * 使用例:
			 *   {
			 *       StackAllocator::ScopedMarker mark(frameStack);
			 *       frameStack.Allocate(256);
			 *   } // ← ここで自動的に巻き戻る
			 */
			class ScopedMarker
			{
			public:
				explicit ScopedMarker(StackAllocator& allocator) noexcept
					: allocator_(&allocator)
					, marker_(allocator.GetMarker())
				{}

				~ScopedMarker() noexcept
				{
					allocator_->FreeToMarker(marker_);
				}

				ScopedMarker(const ScopedMarker&) = delete;
				ScopedMarker& operator=(const ScopedMarker&) = delete;

			private:
				StackAllocator* allocator_;
				Marker          marker_;
			};


		public:
			explicit StackAllocator(size_t capacityBytes)
				: capacity_(capacityBytes)
				, offset_(0)
			{
				memory_ = static_cast<uint8_t*>(_aligned_malloc(capacityBytes, alignof(std::max_align_t)));
				assert(memory_ && "StackAllocator: pre-allocation failed");
			}

			~StackAllocator()
			{
				_aligned_free(memory_);
			}

			StackAllocator(const StackAllocator&) = delete;
			StackAllocator& operator=(const StackAllocator&) = delete;


			void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override
			{
				if (size == 0) {
					return nullptr;
				}

				size_t current = offset_.load(std::memory_order_relaxed);
				size_t aligned;
				size_t next;
				do {
					aligned = (current + alignment - 1) & ~(alignment - 1);
					next    = aligned + size;
					assert(next <= capacity_ && "StackAllocator: out of memory");
					if (next > capacity_) {
						return nullptr;
					}
				} while (!offset_.compare_exchange_weak(
					current, next,
					std::memory_order_acquire,
					std::memory_order_relaxed));

				return memory_ + aligned;
			}

			// 個別解放は不可（ScopedMarker / Reset を使うこと）
			void Deallocate(void*) noexcept override {}


			Marker GetMarker() const noexcept
			{
				return offset_.load(std::memory_order_acquire);
			}

			void FreeToMarker(Marker marker) noexcept
			{
				assert(marker <= capacity_);
				offset_.store(marker, std::memory_order_release);
			}

			void Reset() noexcept
			{
				offset_.store(0, std::memory_order_release);
			}

			size_t GetUsed()     const noexcept { return offset_.load(std::memory_order_relaxed); }
			size_t GetCapacity() const noexcept { return capacity_; }


		private:
			uint8_t*            memory_;
			const size_t        capacity_;
			std::atomic<size_t> offset_;
		};
	}
}
