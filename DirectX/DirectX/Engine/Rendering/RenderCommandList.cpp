#include "RenderCommandList.h"
#include "FrameContext.h"
#include "../Graphics/RenderContext.h"


namespace engine
{
	namespace rendering
	{
		// ---- Page ---------------------------------------------------------------

		bool RenderCommandList::Page::Fits(size_t size, size_t align) const
		{
			size_t aligned = (cursor + align - 1) & ~(align - 1);
			return aligned + size <= PAGE_SIZE;
		}

		void* RenderCommandList::Page::Alloc(size_t size, size_t align)
		{
			size_t aligned = (cursor + align - 1) & ~(align - 1);
			cursor = aligned + size;
			return data + aligned;
		}

		// ---- RenderCommandList --------------------------------------------------

		void* RenderCommandList::Allocate(size_t size, size_t align)
		{
			for (auto& p : pages_)
			{
				if (p->Fits(size, align)) return p->Alloc(size, align);
			}
			auto& page = pages_.emplace_back(std::make_unique<Page>());
			return page->Alloc(size, align);
		}


		void RenderCommandList::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			for (IRenderCommand* cmd : ptrs_)
			{
				cmd->Execute(ctx, fc);
			}
		}


		void RenderCommandList::Reset()
		{
			// Explicitly call destructors — required because commands may hold shared_ptrs.
			for (IRenderCommand* cmd : ptrs_) cmd->~IRenderCommand();
			ptrs_.clear();

			// Reset page cursors; pages themselves are kept for reuse next frame.
			for (auto& p : pages_) p->cursor = 0;
		}
	}
}
