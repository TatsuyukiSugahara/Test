#pragma once
#include "HeapAllocator.h"
#include "StackAllocator.h"
#include "Platform/PlatformBudget.h"
#include <memory>


namespace aq
{
	namespace memory
	{
		/**
		 * アロケータの初期化サイズ設定。
		 * プラットフォームやプロジェクト毎に InitializeParameter 経由で変更可能。
		 * 既定値はビルドターゲットのリソース予算(aq::platform::GetResourceBudget())から
		 * 取るため、Xbox(UWP)ビルドでは自動的に 5GB 予算・8MB スタックになる。
		 *
		 * heapSizeBytes     : 現在は OS 管理 (上限なし)。将来のプール化に備えた予約フィールド。
		 * stackSizeBytes    : StackAllocator が起動時に一度だけ確保するサイズ。
		 * memoryBudgetBytes : メモリ予算上限の設計目標(0 = 上限なし)。ヒープは OS 管理のため
		 *                     強制ではなく、デバッグ表示・予算超過の観測に用いる。
		 */
		struct MemoryConfig
		{
			size_t stackSizeBytes    = aq::platform::GetResourceBudget().stackSizeBytes;
			size_t heapSizeBytes     = 0;               // 0 = OS 管理
			size_t memoryBudgetBytes = aq::platform::GetResourceBudget().memoryBudgetBytes;
		};


		/**
		 * エンジン管理のアロケータを保持するシングルトン。
		 *
		 * aq::Initialize() → MemoryManager::Initialize(config)
		 * aq::Finalize()   → MemoryManager::Finalize()
		 * aq::Update() 末尾 → ResetStackAllocator()  ← オフセットを先頭に戻すだけ
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

			// メモリ予算の設計目標(バイト)。0 = 上限なし。デバッグ表示等に用いる。
			size_t GetMemoryBudgetBytes() const noexcept { return memoryBudgetBytes_; }

			// 現在トラッキング中のヒープ確保バイト数(_DEBUG のみ実数。Release は 0)。
			size_t GetTrackedBytes() const noexcept;
			// メモリ予算(設計目標)を超過しているか。予算 0 または実数不明(Release)なら false。
			bool   IsOverMemoryBudget() const noexcept;

			// フレーム終端で呼び出す。OSへの解放は行わず、オフセットを先頭に戻すだけ。
			void ResetStackAllocator() noexcept { stackAllocator_->Reset(); }

		private:
			explicit MemoryManager(const MemoryConfig& config)
				: stackAllocator_(new StackAllocator(config.stackSizeBytes))
				, memoryBudgetBytes_(config.memoryBudgetBytes)
			{}

			HeapAllocator                  heap_;
			std::unique_ptr<StackAllocator> stackAllocator_;
			size_t                          memoryBudgetBytes_;

			static MemoryManager* instance_;
		};
	}
}
