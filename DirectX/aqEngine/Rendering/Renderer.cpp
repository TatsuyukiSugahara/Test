#include "aq.h"
#include "Renderer.h"
#include "DrawItemCommand.h"
#include "FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Lighting.h"


namespace aq
{
	namespace rendering
	{
		void Renderer::SetShadowRenderer(std::unique_ptr<IShadowRenderer> sr,
		                                 RenderTargetHandle mainRTHandle,
		                                 float mainViewportW, float mainViewportH)
		{
			shadowRenderer_ = std::move(sr);
			mainRTHandle_   = mainRTHandle;
			mainViewportW_  = mainViewportW;
			mainViewportH_  = mainViewportH;
		}


		void Renderer::BuildCommandList(RenderFrame& frame, RenderCommandList& outList,
		                                RenderTargetHandle rtHandle,
		                                float viewportW, float viewportH) const
		{
			// Pass 1: シャドウパス
			if (shadowRenderer_) {
				shadowRenderer_->FillShadowCBData(frame.lighting.directional, frame.shadow);

				shadowRenderer_->BuildShadowCommandList(frame, outList, rtHandle, viewportW, viewportH);
			}

			// Pass 2: メインパス
			for (const RenderItem& item : frame.items) {
				RecordDrawItem(item, frame.camera, outList);
			}
		}


#if _DEBUG
		void Renderer::RenderDebugSync(graphics::RenderContext& context, RenderFrame& frame)
		{
			ConstantBufferPool perDrawPool(sizeof(graphics::VSConstantBuffer));
			ConstantBufferPool materialPool(sizeof(graphics::MaterialCBData));

			auto lightingCB = graphics::GraphicsDevice::Get().CreateConstantBuffer(
				&frame.lighting, sizeof(frame.lighting));
			context.UpdateSubresource(*lightingCB, frame.lighting);

			auto shadowCB = graphics::GraphicsDevice::Get().CreateConstantBuffer(
				&frame.shadow, sizeof(frame.shadow));

			FrameContext fc { &perDrawPool, &materialPool, lightingCB.get(), shadowCB.get() };

			RenderCommandList list;
			BuildCommandList(frame, list, mainRTHandle_, mainViewportW_, mainViewportH_);
			context.UpdateSubresource(*shadowCB, frame.shadow);
			list.Execute(context, fc);
		}
#endif


		void Renderer::RecordDrawItem(
			const RenderItem&  item,
			const CameraData&  camera,
			RenderCommandList& outList) const
		{
			outList.Enqueue<DrawItemCommand>(item, camera);
		}
	}
}
