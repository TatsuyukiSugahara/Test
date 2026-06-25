#include "aq.h"
#include "OceanDrawCommand.h"
#include "FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Lighting.h"


namespace aq
{
	namespace rendering
	{
		OceanDrawCommand::OceanDrawCommand(const OceanRenderItem& item, const CameraData& camera)
			: item_(item)
			, camera_(camera)
		{
		}


		void OceanDrawCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			// b0: per-draw 変換行列 (DrawItemCommand と同じ)
			graphics::IConstantBuffer* drawCB = fc.perDrawCBPool->Allocate();
			if (!drawCB) return;
			graphics::VSConstantBuffer drawData;
			drawData.world      = item_.base.worldMatrix;
			drawData.view       = camera_.viewMatrix;
			drawData.projection = camera_.projectionMatrix;
			ctx.UpdateSubresource(*drawCB, drawData);
			ctx.VSSetConstantBuffer(0, *drawCB);
			ctx.PSSetConstantBuffer(0, *drawCB);

			// b1: ライティング (フレーム先頭で 1 回更新済み)
			if (fc.lightingCB)
			{
				ctx.VSSetConstantBuffer(1, *fc.lightingCB);
				ctx.PSSetConstantBuffer(1, *fc.lightingCB);
			}

			// b2: マテリアル (法線マップフラグなどを保持)
			graphics::IConstantBuffer* matCB = fc.materialCBPool->Allocate();
			if (matCB)
			{
				ctx.UpdateSubresource(*matCB, item_.base.materialCB);
				ctx.PSSetConstantBuffer(2, *matCB);
			}

			// b3: シャドウ (海は現状シャドウ受けなしだが将来のために確保)
			if (fc.shadowCB)
			{
				ctx.VSSetConstantBuffer(3, *fc.shadowCB);
				ctx.PSSetConstantBuffer(3, *fc.shadowCB);
			}

			// b5: OceanCB (Gerstner 波パラメータ・Fresnel 等) — VS/PS 両方にバインド
			if (fc.oceanCBPool)
			{
				graphics::IConstantBuffer* oceanCB = fc.oceanCBPool->Allocate();
				if (oceanCB)
				{
					ctx.UpdateSubresource(*oceanCB, item_.oceanCB);
					ctx.VSSetConstantBuffer(5, *oceanCB);
					ctx.PSSetConstantBuffer(5, *oceanCB);
				}
			}

			// t0-t3: テクスチャ (法線マップ1=t0, 法線マップ2=t1)
			constexpr uint32_t kSlotCount = static_cast<uint32_t>(TextureSlot::Count);
			for (uint32_t slot = 0; slot < kSlotCount; ++slot)
			{
				if (item_.base.textures[slot])
					ctx.PSSetShaderResource(slot, *item_.base.textures[slot]);
				else
					ctx.PSUnsetShaderResource(slot);
			}

			// s0: Wrap サンプラー
			if (item_.base.samplerState)
				ctx.PsSetSampler(0, *item_.base.samplerState);

			ctx.IASetVertexBuffer(*item_.base.vertexBuffer);
			ctx.IASetIndexBuffer(*item_.base.indexBuffer);
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*item_.base.vs);
			ctx.PSSetShader(*item_.base.ps);
			ctx.IASetInputLayout(*item_.base.vs);

			ctx.DrawIndexed(item_.base.indexCount);
		}
	}
}
