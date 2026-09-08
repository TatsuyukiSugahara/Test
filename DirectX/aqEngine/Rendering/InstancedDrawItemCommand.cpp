#include "aq.h"
#include "InstancedDrawItemCommand.h"
#include "FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		InstancedDrawItemCommand::InstancedDrawItemCommand(const InstancedRenderItem& item, const CameraData& camera)
			: item_(item)
			, camera_(camera)
		{
		}


		void InstancedDrawItemCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			if (item_.instanceCount == 0 || !item_.vertexBuffer || !item_.instanceBuffer || !item_.indexBuffer) {
				return;
			}

			// b0: view / projection(world は per-instance ストリームから取るので identity で送る)
			graphics::IConstantBuffer* drawCB = fc.perDrawCBPool->Allocate();
			if (!drawCB) return;
			graphics::VSConstantBuffer drawData;
			drawData.world      = math::Matrix4x4::Identity;
			drawData.view       = camera_.viewMatrix;
			drawData.projection = camera_.projectionMatrix;
			ctx.UpdateSubresource(*drawCB, drawData);
			ctx.VSSetConstantBuffer(0, *drawCB);
			ctx.PSSetConstantBuffer(0, *drawCB);

			// b2: 風揺れ(草)。風パラメータを持たないメッシュ(路面タイル/コイン)には bind しないので、
			// 既存のインスタンス描画は一切変わらない。
			if (item_.windEnabled)
			{
				graphics::IConstantBuffer* windCB = fc.perDrawCBPool->Allocate();
				if (windCB)
				{
					InstancedWindCBData windData = {};
					windData.windParams    = item_.windParams;
					windData.windDirection = item_.windDirection;
					ctx.UpdateSubresource(*windCB, windData);
					ctx.VSSetConstantBuffer(2, *windCB);
				}
			}

			// t0: アルベドテクスチャ + s0: サンプラー(テクスチャ付きメッシュのみ)
			if (item_.albedo) { ctx.PSSetShaderResource(0, *item_.albedo); }
			else              { ctx.PSUnsetShaderResource(0); }
			if (item_.sampler) { ctx.PsSetSampler(0, *item_.sampler); }

			ctx.IASetVertexBuffer(*item_.vertexBuffer);              // slot0: 共有ジオメトリ
			ctx.IASetVertexBufferSlot(1, *item_.instanceBuffer);    // slot1: per-instance ワールド行列
			ctx.IASetIndexBuffer(*item_.indexBuffer);
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*item_.vs);
			ctx.PSSetShader(*item_.ps);
			ctx.IASetInputLayout(*item_.vs);

			ctx.DrawIndexedInstanced(item_.indexCount, item_.instanceCount);
		}
	}
}
