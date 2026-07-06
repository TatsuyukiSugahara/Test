#include "aq.h"
#include "UIBatchRenderCommand.h"
#include "UIRenderPipeline.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderTarget.h"
#include "Rendering/FrameContext.h"

namespace aq
{
	namespace ui
	{
		void UIBatchRenderCommand::Execute(
			graphics::RenderContext& ctx,
			rendering::FrameContext& fc) const
		{
			if (!payload_
				|| payload_->vertices.empty()
				|| payload_->drawRanges.empty()
				|| !payload_->pipeline
				|| !payload_->pipeline->IsReady())
			{
				// 描画なし：次フレーム GBuffer パスのためステートを既定値に戻す
				ctx.OMSetDepthMode(graphics::DepthMode::ReadWrite);
				ctx.OMSetBlendMode(graphics::BlendMode::Opaque);
				return;
			}

			// ポストプロセス後の確定 RT に描画
			if (fc.displayRT.IsValid())
			{
				auto* rt = graphics::GraphicsDevice::Get().GetRenderTarget(fc.displayRT);
				if (rt) ctx.OMSetRenderTargets(1, rt);
			}

			// UI 専用 GPU ステート
			ctx.OMSetDepthMode(graphics::DepthMode::Disabled);
			ctx.OMSetBlendMode(graphics::BlendMode::AlphaBlend);
			ctx.RSSetScissorEnabled(false);

			UIRenderPipeline& pipeline = *payload_->pipeline;

			// VB / IB をアップロード
			pipeline.Upload(payload_->vertices, payload_->indices);

			// VS / IA / VB / IB / サンプラーをバインド
			pipeline.BindCommon(ctx);

			// DrawRange ごとに描画
			UIShaderType currentShaderType = UIShaderType::Standard;
			pipeline.BindPS(ctx, currentShaderType);

			for (const UIDrawRange& range : payload_->drawRanges)
			{
				if (range.indexCount == 0) continue;

				// PS 切り替え
				if (range.shaderType != currentShaderType)
				{
					currentShaderType = range.shaderType;
					pipeline.BindPS(ctx, currentShaderType);
				}

				// テクスチャバインド (DeferredSRV がまだロード未完了の場合はスキップ)
				if (range.texture && range.texture->GetNativeHandle())
					ctx.PSSetShaderResource(0, *range.texture);
				else
					ctx.PSUnsetShaderResource(0);

				// CircleGauge 専用 CB を更新
				if (range.shaderType == UIShaderType::CircleGauge)
				{
					pipeline.UpdateCircleGaugeCB(ctx,
						range.fillAmount, range.startAngle, range.clockwise);
				}

				// SdfText 専用 CB を更新
				if (range.shaderType == UIShaderType::SdfText)
				{
					pipeline.UpdateSdfTextCB(ctx, range.sdfText);
				}

				// DrawIndexed (startIndex オフセットあり、抽象 API 経由)
				ctx.DrawIndexed(range.indexCount, range.indexOffset);
			}

			// テクスチャ SRV をアンバインド (次パスへのハザード防止)
			ctx.PSUnsetShaderResource(0);

			// 次フレーム GBuffer パスのためステートを既定値に戻す
			ctx.OMSetDepthMode(graphics::DepthMode::ReadWrite);
			ctx.OMSetBlendMode(graphics::BlendMode::Opaque);
		}

	} // namespace ui
} // namespace aq
