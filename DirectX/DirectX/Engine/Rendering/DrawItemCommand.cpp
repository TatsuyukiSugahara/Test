#include "DrawItemCommand.h"
#include "FrameContext.h"
#include "../Graphics/RenderContext.h"
#include "../Graphics/GraphicsTypes.h"
#include "../Graphics/Lighting.h"


namespace engine
{
	namespace rendering
	{
		DrawItemCommand::DrawItemCommand(const RenderItem& item, const CameraData& camera)
			: item_(item)
			, camera_(camera)
		{
		}


		void DrawItemCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			// b0: per-draw transform
			graphics::IConstantBuffer* drawCB = fc.perDrawCBPool->Allocate();
			if (!drawCB) return;
			graphics::VSConstantBuffer drawData;
			drawData.world      = item_.worldMatrix;
			drawData.view       = camera_.viewMatrix;
			drawData.projection = camera_.projectionMatrix;
			ctx.UpdateSubresource(*drawCB, drawData);
			ctx.VSSetConstantBuffer(0, *drawCB);
			ctx.PSSetConstantBuffer(0, *drawCB);

			// b1: per-frame lighting (フレーム先頭で更新済み、bind のみ)
			if (fc.lightingCB)
			{
				ctx.VSSetConstantBuffer(1, *fc.lightingCB);
				ctx.PSSetConstantBuffer(1, *fc.lightingCB);
			}

			// b2: per-draw material
			graphics::IConstantBuffer* matCB = fc.materialCBPool->Allocate();
			if (matCB)
			{
				ctx.UpdateSubresource(*matCB, item_.materialCB);
				ctx.PSSetConstantBuffer(2, *matCB);
			}

			// t0-t3: テクスチャ
			constexpr uint32_t kSlotCount = static_cast<uint32_t>(TextureSlot::Count);
			for (uint32_t slot = 0; slot < kSlotCount; ++slot)
			{
				if (item_.textures[slot])
					ctx.PSSetShaderResource(slot, *item_.textures[slot]);
				else
					ctx.PSUnsetShaderResource(slot);
			}

			// s0: サンプラー (全スロット共通)
			if (item_.samplerState)
				ctx.PsSetSampler(0, *item_.samplerState);

			ctx.IASetVertexBuffer(*item_.vertexBuffer);
			ctx.IASetIndexBuffer(*item_.indexBuffer);
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*item_.vs);
			ctx.PSSetShader(*item_.ps);
			ctx.IASetInputLayout(*item_.vs);

			ctx.DrawIndexed(item_.indexCount);
		}
	}
}
