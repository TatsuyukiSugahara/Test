#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "IRenderCommand.h"

namespace engine
{
	namespace graphics { class RenderContext; }

	namespace rendering
	{
		struct FrameContext;

		/**
		 * IRenderCommand を 1 フレーム分のリニアアリーナに記録して再生する。
		 *
		 * メモリレイアウト
		 * ----------------
		 * コマンドは固定サイズのページ（各 64 KB）に placement-new される。
		 * ページは再アロケートされないため、保持ポインタは常に有効。
		 * Reset() はすべてのコマンドのデストラクタを呼び出し（shared_ptr メンバに必要）、
		 * ページを解放せずアロケーションカーソルだけをリセットする——次フレームで再利用する。
		 *
		 * DX12 移行: Execute() を ID3D12GraphicsCommandList の記録に置き換える。
		 */
		class RenderCommandList
		{
		public:
			~RenderCommandList() { Reset(); }

			template<typename T, typename... Args>
			void Enqueue(Args&&... args)
			{
				static_assert(sizeof(T) <= PAGE_SIZE, "コマンドサイズがページサイズを超えています");
				void* mem = Allocate(sizeof(T), alignof(T));
				ptrs_.push_back(new (mem) T(std::forward<Args>(args)...));
			}

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const;
			void Reset();

		private:
			static constexpr size_t PAGE_SIZE = 64 * 1024;

			struct Page
			{
				alignas(std::max_align_t) uint8_t data[PAGE_SIZE];
				size_t cursor = 0;

				bool  Fits(size_t size, size_t align) const;
				void* Alloc(size_t size, size_t align);
			};

			void* Allocate(size_t size, size_t align);

			std::vector<std::unique_ptr<Page>> pages_;
			std::vector<IRenderCommand*>       ptrs_;
		};
	}
}
