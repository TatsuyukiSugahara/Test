#include "aq.h"
#include "Renderer.h"
#include "FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Pipeline/Passes/ShadowPass.h"
#include "Pipeline/Passes/GBufferPass.h"
#include "Pipeline/Passes/PostProcessPass.h"
#include "Pipeline/Passes/SkyPass.h"


namespace aq
{
	namespace rendering
	{
		void Renderer::SetPipeline(std::unique_ptr<RenderPipeline> pipeline,
		                           RenderTargetHandle mainRTHandle,
		                           float mainViewportW, float mainViewportH)
		{
			pipeline_      = std::move(pipeline);
			mainRTHandle_  = mainRTHandle;
			mainViewportW_ = mainViewportW;
			mainViewportH_ = mainViewportH;
		}


		RenderTargetHandle Renderer::GetOutputRT() const
		{
			return pipeline_ ? pipeline_->GetOutputRT() : RenderTargetHandle{};
		}


		IShadowRenderer* Renderer::GetShadowRenderer() const
		{
			if (!pipeline_) { return nullptr; }
			auto* pass = pipeline_->Find<ShadowPass>();
			return pass ? pass->GetShadowRenderer() : nullptr;
		}


		IPostProcessRenderer* Renderer::GetPostProcessRenderer() const
		{
			if (!pipeline_) { return nullptr; }
			auto* pass = pipeline_->Find<PostProcessPass>();
			return pass ? pass->GetPostProcessRenderer() : nullptr;
		}


		IDeferredRenderer* Renderer::GetDeferredRenderer() const
		{
			if (!pipeline_) { return nullptr; }
			auto* pass = pipeline_->Find<GBufferPass>();
			return pass ? pass->GetDeferredRenderer() : nullptr;
		}


		SkyRenderer* Renderer::GetSkyRenderer() const
		{
			if (!pipeline_) { return nullptr; }
			auto* pass = pipeline_->Find<SkyPass>();
			return pass ? pass->GetSkyRenderer() : nullptr;
		}


		void Renderer::BuildCommandList(RenderFrame& frame, RenderCommandList& outList,
		                                RenderTargetHandle rtHandle,
		                                float viewportW, float viewportH) const
		{
			if (!pipeline_) { return; }
			pipeline_->Build(frame, outList, rtHandle, viewportW, viewportH);
		}


		void Renderer::BuildCommandListViews(RenderFrame* frames, const ViewRect* rects, const uint32_t viewCount,
		                                     RenderCommandList& outList, RenderTargetHandle rtHandle,
		                                     float viewportW, float viewportH) const
		{
			if (!pipeline_) { return; }
			pipeline_->BuildViews(frames, rects, viewCount, outList, rtHandle, viewportW, viewportH);
		}


#if _DEBUG
		void Renderer::RenderDebugSync(graphics::RenderContext& context, RenderFrame& frame)
		{
			ConstantBufferPool perDrawPool(sizeof(graphics::VSConstantBuffer));
			ConstantBufferPool materialPool(sizeof(graphics::MaterialCBData));
			ConstantBufferPool bonesPool(128u * 64u);
			ConstantBufferPool oceanPool(sizeof(ocean::OceanCBData));

			auto lightingCB = graphics::GraphicsDevice::Get().CreateConstantBuffer(
				&frame.lighting, sizeof(frame.lighting));
			context.UpdateSubresource(*lightingCB, frame.lighting);

			auto shadowCB = graphics::GraphicsDevice::Get().CreateConstantBuffer(
				&frame.shadow, sizeof(frame.shadow));

			FrameContext fc { &perDrawPool, &materialPool, lightingCB.get(), shadowCB.get(), &bonesPool, &oceanPool };

			RenderCommandList list;
			BuildCommandList(frame, list, mainRTHandle_, mainViewportW_, mainViewportH_);
			context.UpdateSubresource(*shadowCB, frame.shadow);
			list.Execute(context, fc);
		}
#endif
	}
}
