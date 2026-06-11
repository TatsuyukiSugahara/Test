#include "IAllocator.h"
#include <new>


// グローバル operator new/delete をエンジンアロケータにルーティング。
// MemoryManager 未初期化時はフォールバックの HeapAllocator (_aligned_malloc) を使用。

void* operator new(std::size_t size)
{
	void* ptr = engine::memory::GetDefaultAllocator().Allocate(size);
	if (!ptr) throw std::bad_alloc{};
	return ptr;
}

void* operator new[](std::size_t size)
{
	void* ptr = engine::memory::GetDefaultAllocator().Allocate(size);
	if (!ptr) throw std::bad_alloc{};
	return ptr;
}

void operator delete(void* ptr) noexcept
{
	engine::memory::GetDefaultAllocator().Deallocate(ptr);
}

void operator delete[](void* ptr) noexcept
{
	engine::memory::GetDefaultAllocator().Deallocate(ptr);
}

// C++14 以降の sized delete
void operator delete(void* ptr, std::size_t) noexcept
{
	engine::memory::GetDefaultAllocator().Deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
	engine::memory::GetDefaultAllocator().Deallocate(ptr);
}

// nothrow 版 (new(std::nothrow) T)
void* operator new(std::size_t size, std::nothrow_t const&) noexcept
{
	return engine::memory::GetDefaultAllocator().Allocate(size);
}

void* operator new[](std::size_t size, std::nothrow_t const&) noexcept
{
	return engine::memory::GetDefaultAllocator().Allocate(size);
}

void operator delete(void* ptr, std::nothrow_t const&) noexcept
{
	engine::memory::GetDefaultAllocator().Deallocate(ptr);
}

void operator delete[](void* ptr, std::nothrow_t const&) noexcept
{
	engine::memory::GetDefaultAllocator().Deallocate(ptr);
}
