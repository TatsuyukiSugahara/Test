#pragma once
#include "IAllocator.h"
#include <vector>


namespace engine
{
	namespace memory
	{
		/**
		 * IAllocator を STL コンテナに渡すためのアダプタ。
		 * デフォルトコンストラクタでエンジンデフォルトアロケータ (HeapAllocator) を使用。
		 *
		 * 使用例:
		 *   engine::memory::vector<int> v;                                       // デフォルト (heap)
		 *   engine::memory::vector<int> v2(StdAllocatorAdapter<int>(myAlloc));   // カスタム
		 */
		template <typename T>
		class StdAllocatorAdapter
		{
		public:
			using value_type      = T;
			using pointer         = T*;
			using const_pointer   = const T*;
			using size_type       = size_t;
			using difference_type = ptrdiff_t;

			template <typename U>
			struct rebind { using other = StdAllocatorAdapter<U>; };


			StdAllocatorAdapter() noexcept
				: allocator_(&GetDefaultAllocator()) {}

			explicit StdAllocatorAdapter(IAllocator& allocator) noexcept
				: allocator_(&allocator) {}

			template <typename U>
			StdAllocatorAdapter(const StdAllocatorAdapter<U>& other) noexcept
				: allocator_(other.allocator_) {}


			T* allocate(size_t n)
			{
				return static_cast<T*>(allocator_->Allocate(sizeof(T) * n, alignof(T)));
			}

			void deallocate(T* ptr, size_t) noexcept
			{
				allocator_->Deallocate(ptr);
			}

			bool operator==(const StdAllocatorAdapter& other) const noexcept { return allocator_ == other.allocator_; }
			bool operator!=(const StdAllocatorAdapter& other) const noexcept { return allocator_ != other.allocator_; }

			IAllocator* allocator_;
		};


		/**
		 * StackAllocator を使う STL アダプタ。
		 * デフォルトコンストラクタで GetStackAllocator() を参照するため、引数不要。
		 * deallocate は no-op。解放は ResetStackAllocator() (= オフセットを先頭に戻す) で行う。
		 *
		 * 使用例:
		 *   engine::memory::stackVector<int> v; // 引数なしで OK
		 */
		template <typename T>
		class StackAllocatorStdAdapter
		{
		public:
			using value_type      = T;
			using pointer         = T*;
			using const_pointer   = const T*;
			using size_type       = size_t;
			using difference_type = ptrdiff_t;

			template <typename U>
			struct rebind { using other = StackAllocatorStdAdapter<U>; };


			StackAllocatorStdAdapter() noexcept
				: allocator_(&GetStackAllocator()) {}

			template <typename U>
			StackAllocatorStdAdapter(const StackAllocatorStdAdapter<U>& other) noexcept
				: allocator_(other.allocator_) {}


			T* allocate(size_t n)
			{
				return static_cast<T*>(allocator_->Allocate(sizeof(T) * n, alignof(T)));
			}

			// 個別解放は不要。ResetStackAllocator() でオフセットを先頭に戻すことで一括回収される。
			void deallocate(T*, size_t) noexcept {}

			bool operator==(const StackAllocatorStdAdapter& other) const noexcept { return allocator_ == other.allocator_; }
			bool operator!=(const StackAllocatorStdAdapter& other) const noexcept { return allocator_ != other.allocator_; }

			IAllocator* allocator_;
		};


		/**
		 * エンジン提供の vector 型エイリアス。
		 *
		 *   vector<T>      → HeapAllocator  (通常の動的確保)
		 *   stackVector<T> → StackAllocator (毎フレームオフセットリセット、高速・ゼロ解放コスト)
		 */
		template <typename T>
		using vector = std::vector<T, StdAllocatorAdapter<T>>;

		template <typename T>
		using stackVector = std::vector<T, StackAllocatorStdAdapter<T>>;
	}
}
