#include "aq.h"
#include "Renderer.h"
#include "DrawItemCommand.h"
#include "OceanDrawCommand.h"
#include "FrameContext.h"
#include "Occlusion/ClusterCull.h"
#include "Occlusion/GpuClusterCuller.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsTypes.h"
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
			// compute 非対応(FL10 の Xbox One UWP 等)ではポストプロセス(Bloom)が動かないので、
			// シーン RT を直接表示する。
			if (postProcessRenderer_ && graphics::IsComputeSupported()) {
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
				shadowRenderer_->FillShadowCBData(frame.lighting, frame.shadow);
				shadowRenderer_->BuildShadowCommandList(frame, outList, rtHandle, viewportW, viewportH);
			}

			// Pass 1.5: GPU 駆動クラスタ(トライアングル)カリング (compute フェーズ)。
			// 有効時、各アイテムの可視クラスタを compact し間接引数を構築する。
			// build 時に useGpuCull を確定し、描画コマンドと整合させる (1フレームの toggle ズレ防止)。
			if (graphics::IsComputeSupported() && IsClusterCullEnabled() && GpuClusterCuller::Get().IsReady())
			{
				// 小メッシュは dispatch/間接描画の固定コストが削減効果を上回るため閾値でスキップ。
				const uint32_t minClusters = GetClusterCullMinClusters();
				auto runCull = [&](std::vector<RenderItem>& items)
				{
					for (RenderItem& item : items)
					{
						if (item.clusterCount >= minClusters && item.gpuOutIndices && item.gpuArgs)
						{
							item.useGpuCull = true;
							outList.Enqueue<ClusterCullCommand>(item, frame.camera);
						}
					}
				};
				runCull(frame.items);
				runCull(frame.forwardItems);
			}

			if (deferredRenderer_)
			{
				// Pass 2a: G-Buffer パス（deferred items を MRT に書き込む）
				deferredRenderer_->BuildGBufferCommandList(frame, outList);

				// Pass 2a.5: Hi-Z ピラミッド構築（worldPos 確定後・オクリュージョン用）
				if (hiZBuildCallback_)
					hiZBuildCallback_(frame, outList);

				// Pass 2.5: 投影デカールパス（GBuffer0 albedo へ書き戻す。ライティング前）
				deferredRenderer_->BuildDecalCommandList(frame, outList);

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
			// 海は FFT コンピュートに依存するため compute 非対応(FL10)では描画しない。
			if (graphics::IsComputeSupported()) {
				for (const OceanRenderItem& item : frame.oceanItems) {
					outList.Enqueue<OceanDrawCommand>(item, frame.camera);
				}
			}

			// Pass 5: ポストプロセス (compute 非対応では Bloom が動かないのでスキップ)
			if (postProcessRenderer_ && applyPostProcess && graphics::IsComputeSupported()) {
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
