#include "FrameCommands.h"
#include "../Graphics/RenderContext.h"
#include "../Graphics/GraphicsDevice.h"
#include <cassert>


namespace engine
{
	namespace rendering
	{
		SetRenderTargetCommand::SetRenderTargetCommand(uint32_t numViews, const RenderTargetHandle* handles)
			: numViews_(numViews)
		{
			assert(numViews >= 1 && numViews <= MAX_MRT);
			uint32_t count = (numViews <= MAX_MRT) ? numViews : MAX_MRT;
			for (uint32_t i = 0; i < count; ++i)
				handles_[i] = handles[i];
		}


		void SetRenderTargetCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			assert(numViews_ == 1);
			if (numViews_ != 1) return;

			// メイン・オフスクリーン両対応の統合ルックアップ。
			// DX12: RTV デスクリプタ・ヒープから D3D12_CPU_DESCRIPTOR_HANDLE を取得して渡す。
			auto* rt = graphics::GraphicsDevice::Get().GetRenderTarget(handles_[0]);
			assert(rt != nullptr);
			if (!rt) return;

			ctx.OMSetRenderTargets(numViews_, rt);
		}


		void ClearRenderTargetCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			ctx.ClearRenderTargetView(index_, const_cast<float*>(color_));
		}


		void SetViewportCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			ctx.RSSetViewport(x_, y_, w_, h_);
		}
	}
}
