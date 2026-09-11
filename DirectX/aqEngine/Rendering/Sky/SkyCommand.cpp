#include "aq.h"
#include "SkyCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderContextImpl.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			/** キューブマップの SRV スロット (t0-t3=マテリアル / t4=シャドウ / t8-t11=GBuffer) */
			static constexpr uint32_t SKY_CUBEMAP_SLOT = 5;
		}


		SkyCommand::SkyCommand(graphics::IShader&             skyVS,
		                       graphics::IShader&             skyPS,
		                       graphics::ISamplerState&       sampler,
		                       graphics::IShaderResourceView& cubeMap,
		                       const CameraData&              camera,
		                       const math::Vector4&           tint)
			: skyVS_(&skyVS)
			, skyPS_(&skyPS)
			, sampler_(&sampler)
			, cubeMap_(&cubeMap)
			, camera_(camera)
			, tint_(tint)
		{
		}


		void SkyCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			// 空は不透明。深度テストは効かせるが書き込まない
			// (空が Z を塗ると後続のフォワード/海/パーティクルが落ちる)。
			ctx.OMSetBlendMode(graphics::BlendMode::Opaque);
			ctx.OMSetDepthMode(graphics::DepthMode::ReadOnly);

			// b0: SkyCB。view の平行移動成分を 0 にしてから projection と合成し、その逆行列を渡す。
			// 抜かないとカメラの移動で空がずれる (無限遠にならない)。
			// 行列は行ベクトル規約のまま転置せずに積む (DecalComponent.cpp の worldToDecal と同じ。
			// HLSL 側は mul(invViewProjection, float4(ndc, 1, 1)) で受ける)。
			graphics::IConstantBuffer* skyCB = fc.perDrawCBPool->Allocate();
			if (!skyCB) return;

			math::Matrix4x4 viewNoTranslation = camera_.viewMatrix;
			viewNoTranslation._41 = 0.0f;
			viewNoTranslation._42 = 0.0f;
			viewNoTranslation._43 = 0.0f;
			viewNoTranslation._44 = 1.0f;

			math::Matrix4x4 viewProjection;
			viewProjection.Mull(viewNoTranslation, camera_.projectionMatrix);

			SkyCBData cbData;
			cbData.invViewProjection.Inverse(viewProjection);
			cbData.tint = tint_;
			ctx.UpdateSubresource(*skyCB, cbData);
			ctx.VSSetConstantBuffer(0, *skyCB);
			ctx.PSSetConstantBuffer(0, *skyCB);

			// t5/s0: キューブマップ
			ctx.PSSetShaderResource(SKY_CUBEMAP_SLOT, *cubeMap_);
			ctx.PsSetSampler(0, *sampler_);

			// フルスクリーントライアングル (SV_VertexID ベース: 頂点バッファ / 入力レイアウト不要)
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*skyVS_);
			ctx.PSSetShader(*skyPS_);
			ctx.Draw(3, 0);

			// t5 をアンバインドし、DepthMode を既定へ戻す。
			ctx.PSUnsetShaderResource(SKY_CUBEMAP_SLOT);
			ctx.OMSetDepthMode(graphics::DepthMode::ReadWrite);
		}
	}
}
