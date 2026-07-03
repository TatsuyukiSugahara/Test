#pragma once
#include <cstddef>
#include <cstdint>

namespace aq
{
	namespace platform
	{
		// プラットフォーム別のリソース予算。
		// Xbox Dev Mode(道A:UWP「Game」分類)の制限を設計に反映するためのコンパイル時
		// プロファイル。参照: aqEngine/Platform/Xbox移植設計.md §1。
		struct ResourceBudget
		{
			// メモリ予算上限(バイト)。0 = 上限なし(OS 管理)。
			// ヒープは OS 管理のため強制ではなく「設計目標」。over-budget の観測用に保持する。
			size_t   memoryBudgetBytes;
			// 起動時に確保する StackAllocator サイズ。
			size_t   stackSizeBytes;
			// ThreadPool のワーカ数。0 = 論理コア数(hardware_concurrency)。
			uint32_t threadPoolWorkerCount;
			// 単一ファイルの最大サイズ(アセットパック分割の指針)。0 = 制限なし。
			size_t   maxSingleFileBytes;
		};

		// 現在のビルドターゲットのリソース予算を返す(コンパイル時定数)。
		constexpr ResourceBudget GetResourceBudget()
		{
#if defined(AQ_PLATFORM_UWP)
			// Xbox Dev Mode「Game」分類: RAM 5GB / 4コア占有+2コア共有 / 1ファイル2GB。
			// Series S も同一の 5GB 上限のため、安全側に固定する。
			// バックグラウンド 128MB 制限は suspend 時のメモリ解放(Phase 4)で対応。
			return ResourceBudget{
				/*memoryBudgetBytes*/      static_cast<size_t>(5) * 1024 * 1024 * 1024,
				/*stackSizeBytes*/         static_cast<size_t>(8) * 1024 * 1024,
				/*threadPoolWorkerCount*/  6u,
				/*maxSingleFileBytes*/     static_cast<size_t>(2) * 1024 * 1024 * 1024
			};
#else
			// デスクトップ(Win32): 上限なし・論理コア数・ファイル制限なし。
			return ResourceBudget{
				/*memoryBudgetBytes*/      0,
				/*stackSizeBytes*/         static_cast<size_t>(4) * 1024 * 1024,
				/*threadPoolWorkerCount*/  0u,
				/*maxSingleFileBytes*/     0
			};
#endif
		}

		// 単一ファイルのバイト数が予算(maxSingleFileBytes)内か。0(無制限)なら常に true。
		// Win32 は 0 のため常に true = チェック無効。UWP(2GB)でのみ実効。
		constexpr bool IsWithinSingleFileBudget(size_t fileBytes)
		{
			constexpr size_t maxBytes = GetResourceBudget().maxSingleFileBytes;
			return maxBytes == 0 || fileBytes <= maxBytes;
		}
	}
}
