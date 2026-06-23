#include "aq.h"
#include "Renderer.h"
#include "DrawItemCommand.h"
#include "OceanDrawCommand.h"
#include "FrameCommands.h"
#include "FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Lighting.h"
#include "Ocean/OceanData.h"


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


		void Renderer::SetPostProcessRenderer(std::unique_ptr<IPostProcessRenderer> pp)
		{
			postProcessRenderer_ = std::move(pp);
		}


		void Renderer::SetDeferredRenderer(std::unique_ptr<IDeferredRenderer> dr)
		{
			deferredRenderer_ = std::move(dr);
		}


		RenderTargetHandle Renderer::GetDisplayRTHandle(RenderTargetHandle sceneRT) const
		{
			if (postProcessRenderer_) {
				return postProcessRenderer_->GetFinalRT();
			}
			return sceneRT;
		}


		void Renderer::BuildCommandList(RenderFrame& frame, RenderCommandList& outList,
		                                RenderTargetHandle rtHandle,
		                                float viewportW, float viewportH,
		                                bool applyPostProcess) const
		{
			// Pass 1: シャドウパス
			if (shadowRenderer_) {
				shadowRenderer_->FillShadowCBData(frame.lighting.directional, frame.shadow);
				shadowRenderer_->BuildShadowCommandList(frame, outList, rtHandle, viewportW, viewportH);
			}

			if (deferredRenderer_)
			{
				// Pass 2a: G-Buffer パス（deferred items を MRT に書き込む）
				deferredRenderer_->BuildGBufferCommandList(frame, outList);

				// Pass 2b: ディファードライティングパス（シーン RT に書き込む）
				deferredRenderer_->BuildLightingCommandList(frame, outList, rtHandle);

				// Pass 3: フォワードパス（透明・特殊マテリアル）
				// GBuffer0 の depth を使って深度テストしながら描画する
				const RenderTargetHandle gbuffer0 = deferredRenderer_->GetGBuffer0Handle();
				outList.Enqueue<SetRenderTargetWithDepthCommand>(rtHandle, gbuffer0);
				for (const RenderItem& item : frame.forwardItems) {
					RecordDrawItem(item, frame.camera, outList);
				}
			}
			else
			{
				// ディファードなし: 全アイテムをフォワードで描画
				for (const RenderItem& item : frame.items) {
					RecordDrawItem(item, frame.camera, outList);
				}
				for (const RenderItem& item : frame.forwardItems) {
					RecordDrawItem(item, frame.camera, outList);
				}
			}

			// Pass 4: 海パス（フォワードパスの後、ポストプロセスの前）
			for (const OceanRenderItem& item : frame.oceanItems) {
				outList.Enqueue<OceanDrawCommand>(item, frame.camera);
			}

			// Pass 5: ポストプロセス
			if (postProcessRenderer_ && applyPostProcess) {
				postProcessRenderer_->BuildPostProcessCommandList(
					outList, rtHandle,
					static_cast<uint32_t>(viewportW),
					static_cast<uint32_t>(viewportH));
			}

			// Pass 6: UI (ポストプロセス後・ImGui 前に描画)
			if (uiRenderCallback_) {
				uiRenderCallback_(outList);
			}
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


		void Renderer::RecordDrawItem(
			const RenderItem&  item,
			const CameraData&  camera,
			RenderCommandList& outList) const
		{
			outList.Enqueue<DrawItemCommand>(item, camera);
		}
	}
}
