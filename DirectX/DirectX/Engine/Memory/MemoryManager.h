#pragma once
#include "HeapAllocator.h"
#include "StackAllocator.h"


namespace engine
{
	namespace memory
	{
		/**
		 * アロケータの初期化サイズ設定。
		 * プラットフォームやプロジェクト毎に InitializeParameter 経由で変更可能。
		 *
		 * heapSizeBytes : 現在は OS 管理 (上限なし)。将来のプール化に備えた予約フィールド。
		 * stackSizeBytes: StackAllocator が起動時に一度だけ確保するサイズ。
		 */
		struct MemoryConfig
		{
			size_t stackSizeBytes = 4 * 1024 * 1024; // 4 MB
			size_t heapSizeBytes  = 0;               // 0 = OS 管理
		};


		/**
		 * エンジン管理のアロケータを保持するシングルトン。
		 *
		 * Engine::Initialize() → MemoryManager::Initialize(config)
		 * Engine::Finalize()   → MemoryManager::Finalize()
		 * Engine::Update() 末尾 → ResetStackAllocator()  ← オフセットを先頭に戻すだけ
		 */
		class MemoryManager
		{
		public:
			static void           Initialize(const MemoryConfig& config = MemoryConfig{});
			static void           Finalize();
			static MemoryManager& Get();
			static bool           IsInitialized() noexcept { return instance_ != nullptr; }

			IAllocator&     GetHeapAllocator()  noexcept { return heap_; }
			StackAllocator& GetStackAllocator() noexcept { return *stackAllocator_; }

			// フレーム終端で呼び出す。OSへの解放は行わず、オフセットを先頭に戻すだけ。
			void ResetStackAllocator() noexcept { stackAllocator_->Reset(); }

		private:
			explicit MemoryManager(const MemoryConfig& config)
				: stackAllocator_(new StackAllocator(config.stackSizeBytes))
			{}

			HeapAllocator   heap_;
			StackAllocator* stackAllocator_;

			static MemoryManager* instance_;
		};
	}
}
