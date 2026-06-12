#include "Renderer.h"
#include "../Graphics/RenderContext.h"
#include "../Graphics/GraphicsTypes.h"


namespace engine
{
	namespace rendering
	{
		void Renderer::Render(graphics::RenderContext& context, const RenderFrame& frame)
		{
			for (const RenderItem& item : frame.items)
			{
				DrawItem(context, item, frame.camera);
			}
		}


		void Renderer::DrawItem(
			graphics::RenderContext& context,
			const RenderItem&        item,
			const CameraData&        camera)
		{
			graphics::VSConstantBuffer cb;
			cb.world      = item.worldMatrix;
			cb.view       = camera.viewMatrix;
			cb.projection = camera.projectionMatrix;
			context.UpdateSubresource(*item.constantBuffer, cb);
			context.VSSetConstantBuffer(0, *item.constantBuffer);
			context.PSSetConstantBuffer(0, *item.constantBuffer);

			context.IASetVertexBuffer(*item.vertexBuffer);
			context.IASetIndexBuffer(*item.indexBuffer);
			context.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);

			if (item.texture && item.samplerState)
			{
				context.PSSetShaderResource(0, *item.texture);
				context.PsSetSampler(0, *item.samplerState);
			}

			context.VSSetShader(*item.vs);
			context.PSSetShader(*item.ps);
			context.IASetInputLayout(*item.vs);

			context.DrawIndexed(item.indexCount);
		}
	}
}
