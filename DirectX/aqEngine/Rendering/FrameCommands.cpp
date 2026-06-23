#include "aq.h"
#include "FrameCommands.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsDevice.h"
#include <cassert>


namespace aq
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
			if (numViews_ == 1) {
				// 単一 RT: 既存パスをそのまま使用
				auto* rt = graphics::GraphicsDevice::Get().GetRenderTarget(handles_[0]);
				assert(rt != nullptr);
				if (!rt) return;
				ctx.OMSetRenderTargets(1u, rt);
			} else {
				// MRT: 各ハンドルを個別に解決して配列を構成
				graphics::IRenderTarget* rts[MAX_MRT] = {};
				for (uint32_t i = 0; i < numViews_; ++i) {
					rts[i] = graphics::GraphicsDevice::Get().GetRenderTarget(handles_[i]);
				}
				ctx.OMSetMRTRenderTargets(numViews_, rts);
			}
		}


		void SetRenderTargetWithDepthCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			auto* colorRT = graphics::GraphicsDevice::Get().GetRenderTarget(colorHandle_);
			auto* depthRT = graphics::GraphicsDevice::Get().GetRenderTarget(depthHandle_);
			assert(colorRT && depthRT);
			if (!colorRT || !depthRT) return;
			ctx.OMSetRenderTargetWithDepth(*colorRT, *depthRT);
		}


		void ClearRenderTargetCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			ctx.ClearRenderTargetView(index_, const_cast<float*>(color_));
		}


		void ClearDepthCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			ctx.ClearDepthBuffer();
		}


		void SetViewportCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			ctx.RSSetViewport(x_, y_, w_, h_);
		}
	}
}
