#include "aq.h"
#include "MemoryManager.h"
#include <cassert>

#ifdef _DEBUG
#include "MemoryTracker.h"
#endif


namespace aq
{
	namespace memory
	{
		MemoryManager* MemoryManager::instance_ = nullptr;


		void MemoryManager::Initialize(const MemoryConfig& config)
		{
			assert(!instance_ && "MemoryManager::Initialize called twice");
			instance_ = new MemoryManager(config);
		}

		void MemoryManager::Finalize()
		{
			// instance_ を先にクリアする。
			// 以降の operator delete はフォールバック HeapAllocator を使うため
			// MemoryManager 自身の解放が「リーク」として残らない。
			MemoryManager* tmp = instance_;
			instance_ = nullptr;
			delete tmp;

#ifdef _DEBUG
			ReportLeaks();
#endif
		}

		MemoryManager& MemoryManager::Get()
		{
			assert(instance_ && "MemoryManager not initialized");
			return *instance_;
		}


		// IAllocator.h で宣言された関数の実装。
		// MemoryManager 未初期化時 (main 実行前の静的初期化など) はフォールバックの HeapAllocator を返す。
		IAllocator& GetDefaultAllocator() noexcept
		{
			if (MemoryManager::IsInitialized()) {
				return MemoryManager::Get().GetHeapAllocator();
			}
			static HeapAllocator s_fallback;
			return s_fallback;
		}

		StackAllocator& GetStackAllocator() noexcept
		{
			return MemoryManager::Get().GetStackAllocator();
		}
	}
}
