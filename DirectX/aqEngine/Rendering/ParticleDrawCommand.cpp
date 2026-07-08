#include "aq.h"
#include "ParticleDrawCommand.h"
#include "FrameContext.h"
#include "Graphics/IRenderContextImpl.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		ParticleDrawCommand::ParticleDrawCommand(const ParticleRenderItem& item, const CameraData& camera)
			: item_(item)
			, camera_(camera)
		{
		}


		void ParticleDrawCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			if (!item_.vertexBuffer || !item_.indexBuffer || !item_.vs || !item_.ps || item_.indexCount == 0)
				return;

			ctx.OMSetBlendMode(item_.additive ? graphics::BlendMode::Additive : graphics::BlendMode::AlphaBlend);
			// 半透明は深度テストのみ (書き込みOFF)。透明部分が Z を塗って
			// 後続の粒/グローを切らないようにする。描画後に既定へ戻す。
			ctx.OMSetDepthMode(graphics::DepthMode::ReadOnly);

			// b0: view/proj (world=Identity。頂点は CPU でワールド展開済み)
			graphics::IConstantBuffer* drawCB = fc.perDrawCBPool->Allocate();
			if (!drawCB) return;
			graphics::VSConstantBuffer drawData;
			drawData.world      = math::Matrix4x4::Identity;
			drawData.view       = camera_.viewMatrix;
			drawData.projection = camera_.projectionMatrix;
			ctx.UpdateSubresource(*drawCB, drawData);
			ctx.VSSetConstantBuffer(0, *drawCB);
			ctx.PSSetConstantBuffer(0, *drawCB);

			// 動的VBへの頂点書き込みはここ (レンダースレッド) で行う。
			// ゲームスレッドから Map すると D3D11 は immediate context の競合、
			// D3D12 は書き込み時と描画時のフレームインデックス不一致で頂点が破れる。
			if (item_.vertices && !item_.vertices->empty()) {
				item_.vertexBuffer->Update(item_.vertices->data(),
					static_cast<uint32_t>(item_.vertices->size() * sizeof(ParticleVertex)));
			}

			// t0/s0: テクスチャ (未指定なら手続き円 PS のため束縛しない)
			if (item_.texture) {
				ctx.PSSetShaderResource(0, *item_.texture);
				if (item_.samplerState)
					ctx.PsSetSampler(0, *item_.samplerState);
			}

			ctx.IASetVertexBuffer(*item_.vertexBuffer);
			ctx.IASetIndexBuffer(*item_.indexBuffer);
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*item_.vs);
			ctx.PSSetShader(*item_.ps);
			ctx.IASetInputLayout(*item_.vs);

			ctx.DrawIndexed(item_.indexCount);

			// 深度書き込みを既定へ戻す (blend は Renderer 側の SetBlendModeCommand が Opaque へ戻す)
			ctx.OMSetDepthMode(graphics::DepthMode::ReadWrite);
		}
	}
}
