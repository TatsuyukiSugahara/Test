#include "aq.h"
#include "DeferredLightingCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderContextImpl.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/IRenderTarget.h"
#include "Graphics/Lighting.h"
#include "Rendering/IBL/IBLEnvironment.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			/** IBL のスロット (t4=シャドウ / t8-t11=GBuffer の間の空き。設計書/IBL設計.md §0.1) */
			static constexpr uint32_t IBL_IRRADIANCE_SLOT = 5;
			static constexpr uint32_t IBL_SPECULAR_SLOT   = 6;
			static constexpr uint32_t IBL_BRDF_LUT_SLOT   = 7;
			static constexpr uint32_t IBL_SAMPLER_SLOT    = 0;
			static constexpr uint32_t IBL_CB_SLOT         = 2;
		}


		DeferredLightingCommand::DeferredLightingCommand(
			graphics::IShader& lightingVS,
			graphics::IShader& lightingPS,
			RenderTargetHandle  gbuffer0Handle,
			RenderTargetHandle  gbuffer1Handle,
			RenderTargetHandle  gbuffer2Handle,
			RenderTargetHandle  gbuffer3Handle,
			const CameraData&  camera,
			std::shared_ptr<const IBLEnvironment> ibl,
			const float                           iblIntensity)
			: lightingVS_(&lightingVS)
			, lightingPS_(&lightingPS)
			, gbuffer0Handle_(gbuffer0Handle)
			, gbuffer1Handle_(gbuffer1Handle)
			, gbuffer2Handle_(gbuffer2Handle)
			, gbuffer3Handle_(gbuffer3Handle)
			, camera_(camera)
			, ibl_(std::move(ibl))
			, iblIntensity_(iblIntensity)
		{
		}


		void DeferredLightingCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			// Execute() 時に Handle を解決（コマンドキュー生成後の RT 再配置に対応）
			auto& gd      = graphics::GraphicsDevice::Get();
			auto* gbuffer0 = gd.GetRenderTarget(gbuffer0Handle_);
			auto* gbuffer1 = gd.GetRenderTarget(gbuffer1Handle_);
			auto* gbuffer2 = gd.GetRenderTarget(gbuffer2Handle_);
			auto* gbuffer3 = gd.GetRenderTarget(gbuffer3Handle_);
			if (!gbuffer0 || !gbuffer1 || !gbuffer2 || !gbuffer3) return;

			// IBL の資源を先にそろえる。lightingPS は PSMainIBL なので、1 つでも欠けたら描かない
			// (未バインドのまま描くと Metal / Vulkan で型の合わないフォールバックが刺さる)。
			// 状態を変える前に判定し、途中で抜けても DepthMode 等を汚さない。
			graphics::IShaderResourceView* iblIrradiance = nullptr;
			graphics::IShaderResourceView* iblSpecular   = nullptr;
			graphics::IShaderResourceView* iblBrdfLut    = nullptr;
			graphics::ISamplerState*       iblSampler    = nullptr;
			graphics::IConstantBuffer*     iblCB         = nullptr;
			if (ibl_)
			{
				iblIrradiance = ibl_->GetIrradianceSRV();
				iblSpecular   = ibl_->GetSpecularSRV();
				iblBrdfLut    = ibl_->GetBrdfLutSRV();
				iblSampler    = ibl_->GetSampler();
				if (!iblIrradiance || !iblSpecular || !iblBrdfLut || !iblSampler) return;

				// CB はフレームスロットごとのプールから取る (GPU 実行中の CB を上書きしない。§1.1)
				iblCB = fc.perDrawCBPool->Allocate();
				if (!iblCB) return;
			}

			// DepthEnable を FALSE にしてフルスクリーン描画（デプスバッファに書き込まない）
			ctx.OMSetDepthMode(graphics::DepthMode::Disabled);

			// b0: カメラ情報（fullscreen VS にも渡す。不要な場合は PS 側で無視される）
			graphics::IConstantBuffer* drawCB = fc.perDrawCBPool->Allocate();
			if (drawCB)
			{
				graphics::VSConstantBuffer drawData;
				drawData.world      = math::Matrix4x4::Identity;
				drawData.view       = camera_.viewMatrix;
				drawData.projection = camera_.projectionMatrix;
				ctx.UpdateSubresource(*drawCB, drawData);
				ctx.VSSetConstantBuffer(0, *drawCB);
				ctx.PSSetConstantBuffer(0, *drawCB);
			}

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

			// t8-t11: GBuffer SRV をバインド
			ctx.PSSetShaderResource(8,  gbuffer0->GetRenderTargetSRV());
			ctx.PSSetShaderResource(9,  gbuffer1->GetRenderTargetSRV());
			ctx.PSSetShaderResource(10, gbuffer2->GetRenderTargetSRV());
			ctx.PSSetShaderResource(11, gbuffer3->GetRenderTargetSRV());

			// b2 / t5-t7 / s0: IBL (PSMainIBL のときだけ。PSMain はこれらを参照しない)
			if (ibl_)
			{
				IBLCBData iblData;
				iblData.intensity      = iblIntensity_;
				iblData.specularMaxMip = ibl_->GetSpecularMaxMip();
				ctx.UpdateSubresource(*iblCB, iblData);
				ctx.PSSetConstantBuffer(IBL_CB_SLOT, *iblCB);

				ctx.PSSetShaderResource(IBL_IRRADIANCE_SLOT, *iblIrradiance);
				ctx.PSSetShaderResource(IBL_SPECULAR_SLOT,   *iblSpecular);
				ctx.PSSetShaderResource(IBL_BRDF_LUT_SLOT,   *iblBrdfLut);
				ctx.PsSetSampler(IBL_SAMPLER_SLOT, *iblSampler);
			}

			// フルスクリーントライアングル (SV_VertexID ベース: 頂点バッファ不要)
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*lightingVS_);
			ctx.PSSetShader(*lightingPS_);
			ctx.Draw(3, 0);

			// t8-t11 アンバインド（次パスで RT → SRV 競合を防ぐ）
			ctx.PSUnsetShaderResource(8);
			ctx.PSUnsetShaderResource(9);
			ctx.PSUnsetShaderResource(10);
			ctx.PSUnsetShaderResource(11);

			// t5-t7 アンバインド（後続の空パスが t5 を別のキューブで使う）
			if (ibl_)
			{
				ctx.PSUnsetShaderResource(IBL_IRRADIANCE_SLOT);
				ctx.PSUnsetShaderResource(IBL_SPECULAR_SLOT);
				ctx.PSUnsetShaderResource(IBL_BRDF_LUT_SLOT);
			}

			// DepthMode を既定値に戻す
			ctx.OMSetDepthMode(graphics::DepthMode::ReadWrite);
		}
	}
}
