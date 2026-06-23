#include "aq.h"
#include "GBufferItemCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Lighting.h"
#include <array>
#include <cstring>
#include <algorithm>


namespace aq
{
	namespace rendering
	{
		GBufferItemCommand::GBufferItemCommand(const RenderItem& item, const CameraData& camera)
			: item_(item)
			, camera_(camera)
		{
		}


		void GBufferItemCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			if (!item_.gbufferPS) return;

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

			// b1: per-frame lighting
			if (fc.lightingCB)
			{
				ctx.VSSetConstantBuffer(1, *fc.lightingCB);
				ctx.PSSetConstantBuffer(1, *fc.lightingCB);
			}

			// b3: per-frame shadow
			if (fc.shadowCB)
			{
				ctx.VSSetConstantBuffer(3, *fc.shadowCB);
				ctx.PSSetConstantBuffer(3, *fc.shadowCB);
			}

			// b2: per-draw material
			graphics::IConstantBuffer* matCB = fc.materialCBPool->Allocate();
			if (matCB)
			{
				ctx.UpdateSubresource(*matCB, item_.materialCB);
				ctx.PSSetConstantBuffer(2, *matCB);
			}

			// b4: bone matrices (skeletal mesh only)
			if (item_.boneMatrices && !item_.boneMatrices->empty() && fc.bonesCBPool)
			{
				graphics::IConstantBuffer* bonesCB = fc.bonesCBPool->Allocate();
				if (bonesCB)
				{
					std::array<math::Matrix4x4, 128> bonesData;
					bonesData.fill(math::Matrix4x4::Identity);
					const uint32_t count = std::min(
						static_cast<uint32_t>(item_.boneMatrices->size()), 128u);
					std::memcpy(bonesData.data(), item_.boneMatrices->data(),
						count * sizeof(math::Matrix4x4));
					ctx.UpdateSubresource(*bonesCB, bonesData);
					ctx.VSSetConstantBuffer(4, *bonesCB);
				}
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

			// s0: サンプラー
			if (item_.samplerState)
				ctx.PsSetSampler(0, *item_.samplerState);

			ctx.IASetVertexBuffer(*item_.vertexBuffer);
			ctx.IASetIndexBuffer(*item_.indexBuffer);
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*item_.vs);
			ctx.PSSetShader(*item_.gbufferPS);   // G-Buffer PS を使用
			ctx.IASetInputLayout(*item_.vs);

			ctx.DrawIndexed(item_.indexCount);
		}
	}
}
