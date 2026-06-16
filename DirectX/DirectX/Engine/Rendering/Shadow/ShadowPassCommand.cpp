#include "ShadowPassCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/GraphicsTypes.h"


namespace engine
{
	namespace rendering
	{
		// ----------------------------------------------------------------
		// ShadowBeginCommand
		// ----------------------------------------------------------------

		ShadowBeginCommand::ShadowBeginCommand(graphics::IDepthMap& depthMap, uint32_t resolution)
			: depthMap_(&depthMap)
			, resolution_(resolution)
		{}


		void ShadowBeginCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			// 前フレームで t4 に bind したシャドウ SRV を先に解放してから DSV として使う
			ctx.PSUnsetShaderResource(4);
			ctx.OMSetDepthOnlyTarget(*depthMap_);
			ctx.RSSetViewport(0.f, 0.f, static_cast<float>(resolution_), static_cast<float>(resolution_));
			ctx.ClearDepthMap(*depthMap_);
		}


		// ----------------------------------------------------------------
		// ShadowCastCommand
		// ----------------------------------------------------------------

		ShadowCastCommand::ShadowCastCommand(const RenderItem& item,
		                                     std::shared_ptr<graphics::IShader> shadowVS)
			: item_(item)
			, shadowVS_(std::move(shadowVS))
		{}


		void ShadowCastCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			// b0: world 行列のみ使用 (既存プールを再利用、view/proj は未使用)
			graphics::IConstantBuffer* drawCB = fc.perDrawCBPool->Allocate();
			if (!drawCB) return;
			graphics::VSConstantBuffer drawData;
			drawData.world = item_.worldMatrix;
			ctx.UpdateSubresource(*drawCB, drawData);
			ctx.VSSetConstantBuffer(0, *drawCB);

			// b3: シャドウ CB (lightViewProj など)
			if (fc.shadowCB) {
				ctx.VSSetConstantBuffer(3, *fc.shadowCB);
			}

			ctx.IASetVertexBuffer(*item_.vertexBuffer);
			ctx.IASetIndexBuffer(*item_.indexBuffer);
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*shadowVS_);
			ctx.PSUnsetShader();
			ctx.IASetInputLayout(*shadowVS_);
			ctx.DrawIndexed(item_.indexCount);
		}


		// ----------------------------------------------------------------
		// ShadowEndCommand
		// ----------------------------------------------------------------

		ShadowEndCommand::ShadowEndCommand(graphics::IDepthMap& depthMap,
		                                   RenderTargetHandle   prevHandle,
		                                   float                prevW,
		                                   float                prevH)
			: depthMap_(&depthMap)
			, prevHandle_(prevHandle)
			, prevW_(prevW)
			, prevH_(prevH)
		{}


		void ShadowEndCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			// 直前の RT を Execute 時に解決して復元 (DSV がアンバインドされ SRV として使用可能になる)
			auto* prevRT = graphics::GraphicsDevice::Get().GetRenderTarget(prevHandle_);
			ctx.OMSetRenderTargets(1, prevRT);
			ctx.RSSetViewport(0.f, 0.f, prevW_, prevH_);

			// t4: シャドウマップ SRV、s1: 比較サンプラー
			if (auto* srv = depthMap_->GetSRV()) {
				ctx.PSSetShaderResource(4, *srv);
			}
			if (auto* sampler = depthMap_->GetSampler()) {
				ctx.PsSetSampler(1, *sampler);
			}
		}
	}
}
