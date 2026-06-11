#pragma once
#include "IAllocator.h"
#include <cstddef>
#include <type_traits>
#include <cassert>
#include <new>


namespace engine
{
	namespace memory
	{
		/**
		 * ランタイムサイズの固定長配列。
		 * メンバ変数は型だけで宣言し、Create() で要素数とアロケータを指定する。
		 * アロケータを省略するとエンジンデフォルト (HeapAllocator) を使用する。
		 *
		 * 使用例:
		 *   AllocatedArray<Transform> transforms;
		 *   transforms.Create(1000);               // デフォルトアロケータ
		 *   transforms.Create(1000, frameStack);   // StackAllocator 指定
		 */
		template <typename T>
		class AllocatedArray
		{
		public:
			AllocatedArray() = default;

			~AllocatedArray()
			{
				Destroy();
			}

			AllocatedArray(const AllocatedArray&) = delete;
			AllocatedArray& operator=(const AllocatedArray&) = delete;

			AllocatedArray(AllocatedArray&& other) noexcept
				: data_(other.data_)
				, size_(other.size_)
				, allocator_(other.allocator_)
			{
				other.data_      = nullptr;
				other.size_      = 0;
				other.allocator_ = nullptr;
			}

			AllocatedArray& operator=(AllocatedArray&& other) noexcept
			{
				if (this != &other) {
					Destroy();
					data_      = other.data_;
					size_      = other.size_;
					allocator_ = other.allocator_;
					other.data_      = nullptr;
					other.size_      = 0;
					other.allocator_ = nullptr;
				}
				return *this;
			}


			/**
			 * 指定要素数で配列を確保してデフォルトコンストラクト。
			 * allocator を省略するとエンジンデフォルトアロケータを使用する。
			 */
			void Create(size_t size, IAllocator& allocator = GetDefaultAllocator())
			{
				Destroy();
				if (size == 0) {
					return;
				}

				allocator_ = &allocator;
				size_      = size;
				data_      = static_cast<T*>(allocator.Allocate(sizeof(T) * size, alignof(T)));
				assert(data_ && "AllocatedArray::Create: allocation failed");

				if constexpr (!std::is_trivially_constructible_v<T>) {
					for (size_t i = 0; i < size_; ++i) {
						::new(data_ + i) T();
					}
				}
			}

			void Destroy() noexcept
			{
				if (!data_) {
					return;
				}

				if constexpr (!std::is_trivially_destructible_v<T>) {
					for (size_t i = size_; i > 0; --i) {
						data_[i - 1].~T();
					}
				}

				allocator_->Deallocate(data_);
				data_      = nullptr;
				size_      = 0;
				allocator_ = nullptr;
			}


			T&       operator[](size_t index)       { assert(index < size_); return data_[index]; }
			const T& operator[](size_t index) const { assert(index < size_); return data_[index]; }

			T*       Data()    { return data_; }
			const T* Data()    const { return data_; }
			size_t   Size()    const { return size_; }
			bool     IsEmpty() const { return size_ == 0; }

			T*       begin()       { return data_; }
			T*       end()         { return data_ + size_; }
			const T* begin() const { return data_; }
			const T* end()   const { return data_ + size_; }


		private:
			T*          data_      = nullptr;
			size_t      size_      = 0;
			IAllocator* allocator_ = nullptr;
		};
	}
}
