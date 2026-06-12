#include "DrawItemCommand.h"
#include "FrameContext.h"
#include "../Graphics/RenderContext.h"
#include "../Graphics/GraphicsTypes.h"


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
			// Allocate a per-draw constant buffer from the frame pool.
			// Each call to Allocate() returns a unique slot for this draw; the pool is
			// reset at the start of each frame, so no persistent allocation occurs.
			graphics::IConstantBuffer* cb = fc.constantBufferPool->Allocate();
			if (!cb) return;  // skip draw on device lost / OOM

			graphics::VSConstantBuffer data;
			data.world      = item_.worldMatrix;
			data.view       = camera_.viewMatrix;
			data.projection = camera_.projectionMatrix;
			ctx.UpdateSubresource(*cb, data);
			ctx.VSSetConstantBuffer(0, *cb);
			ctx.PSSetConstantBuffer(0, *cb);

			ctx.IASetVertexBuffer(*item_.vertexBuffer);
			ctx.IASetIndexBuffer(*item_.indexBuffer);
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);

			if (item_.texture && item_.samplerState)
			{
				ctx.PSSetShaderResource(0, *item_.texture);
				ctx.PsSetSampler(0, *item_.samplerState);
			}

			ctx.VSSetShader(*item_.vs);
			ctx.PSSetShader(*item_.ps);
			ctx.IASetInputLayout(*item_.vs);

			ctx.DrawIndexed(item_.indexCount);
		}
	}
}
