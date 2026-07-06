#include "aq.h"
#include "DeferredDecalCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderContextImpl.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace rendering
	{
		DeferredDecalCommand::DeferredDecalCommand(
			graphics::IShader&       decalVS,
			graphics::IShader&       decalPS,
			graphics::ISamplerState& sampler,
			const DecalRenderItem&   item,
			RenderTargetHandle       gbuffer1Handle,
			RenderTargetHandle       gbuffer2Handle,
			RenderTargetHandle       gbuffer3Handle)
			: decalVS_(&decalVS)
			, decalPS_(&decalPS)
			, sampler_(&sampler)
			, item_(item)
			, gbuffer1Handle_(gbuffer1Handle)
			, gbuffer2Handle_(gbuffer2Handle)
			, gbuffer3Handle_(gbuffer3Handle)
		{
		}


		void DeferredDecalCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			if (!item_.texture) return;

			auto& gd   = graphics::GraphicsDevice::Get();
			auto* gb1  = gd.GetRenderTarget(gbuffer1Handle_);
			auto* gb2  = gd.GetRenderTarget(gbuffer2Handle_);
			auto* gb3  = gd.GetRenderTarget(gbuffer3Handle_);
			if (!gb1 || !gb2 || !gb3) return;

			// 深度テスト無効・RGB のみアルファ合成 (metallic を保護)
			ctx.OMSetDepthMode(graphics::DepthMode::Disabled);
			ctx.OMSetBlendMode(graphics::BlendMode::DecalColor);

			// b0: デカール定数バッファ (perDrawCBPool = 192B から確保)
			graphics::IConstantBuffer* decalCB = fc.perDrawCBPool->Allocate();
			if (!decalCB) return;
			ctx.UpdateSubresource(*decalCB, item_.cb);
			ctx.PSSetConstantBuffer(0, *decalCB);

			// t0: デカールテクスチャ / t9-t11: GBuffer SRV
			ctx.PSSetShaderResource(0,  *item_.texture);
			ctx.PSSetShaderResource(9,  gb1->GetRenderTargetSRV());
			ctx.PSSetShaderResource(10, gb2->GetRenderTargetSRV());
			ctx.PSSetShaderResource(11, gb3->GetRenderTargetSRV());
			ctx.PsSetSampler(0, *sampler_);

			// フルスクリーントライアングル (SV_VertexID ベース: 頂点バッファ不要)
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*decalVS_);
			ctx.PSSetShader(*decalPS_);
			ctx.Draw(3, 0);

			// SRV をアンバインド (次パスで RT → SRV 競合を防ぐ)
			ctx.PSUnsetShaderResource(0);
			ctx.PSUnsetShaderResource(9);
			ctx.PSUnsetShaderResource(10);
			ctx.PSUnsetShaderResource(11);
		}
	}
}
