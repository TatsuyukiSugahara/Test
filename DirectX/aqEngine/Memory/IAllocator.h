#pragma once
#include <cstddef>
#include <utility>
#include <new>

#ifdef _DEBUG
#include "MemoryTracker.h"
#endif


namespace aq
{
	namespace memory
	{
		class IAllocator
		{
		public:
			virtual ~IAllocator() = default;

			virtual void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;
			virtual void  Deallocate(void* ptr) noexcept = 0;
		};

		/**
		 * このスコープ内の確保を「プロセス寿命」として印付けし、リーク報告から外す。
		 *
		 * 静的レジストリ・シングルトン・thread_local キャッシュのように、プロセス終了まで
		 * 生きているのが正常なものに使う。終了時の生存を全部リークとして並べると、
		 * 本当に解放し忘れたものがその中に埋もれるため。
		 * バイト数の集計(GetTrackedBytes)には従来どおり含まれる — 実在するメモリだから。
		 * Release ビルドでは何もしない。
		 */
		class ScopedPersistentAlloc
		{
		public:
#ifdef _DEBUG
			ScopedPersistentAlloc()  noexcept { PushPersistentAlloc(); }
			~ScopedPersistentAlloc() noexcept { PopPersistentAlloc(); }
#else
			ScopedPersistentAlloc()  noexcept {}
			~ScopedPersistentAlloc() noexcept {}
#endif

			ScopedPersistentAlloc(const ScopedPersistentAlloc&)            = delete;
			ScopedPersistentAlloc& operator=(const ScopedPersistentAlloc&) = delete;
		};


		class StackAllocator;

		// 定義は MemoryManager.cpp にある
		IAllocator&     GetDefaultAllocator() noexcept;
		StackAllocator& GetStackAllocator()   noexcept;


		template <typename T, typename... Args>
		T* Construct(IAllocator& allocator, Args&&... args)
		{
			void* ptr = allocator.Allocate(sizeof(T), alignof(T));
			return ::new(ptr) T(std::forward<Args>(args)...);
		}

		template <typename T>
		void Destroy(IAllocator& allocator, T* ptr) noexcept
		{
			if (ptr) {
				ptr->~T();
				allocator.Deallocate(ptr);
			}
		}
	}
}


// placement new/delete — 指定アロケータへ割り当て (グローバル名前空間)
// 使用例: new(aq::memory::GetStackAllocator()) MyClass(args)
inline void* operator new(std::size_t size, aq::memory::IAllocator& alloc)
{
	return alloc.Allocate(size);
}
inline void operator delete(void* ptr, aq::memory::IAllocator& alloc) noexcept
{
	alloc.Deallocate(ptr);
}


// アロケータ明示指定マクロ
// デバッグ: ソース情報 (ファイル/行/関数名) を記録して leak 報告に表示する
// リリース: 通常の placement new と同等
#ifdef _DEBUG
#define engineNewWith(alloc, T, ...) \
	( ::aq::memory::SetNextAllocSource(__FILE__, __LINE__, __FUNCTION__) , new((alloc)) T(__VA_ARGS__) )
#else
#define engineNewWith(alloc, T, ...) new((alloc)) T(__VA_ARGS__)
#endif

#define engineDeleteWith(alloc, ptr) ::aq::memory::Destroy((alloc), (ptr))
