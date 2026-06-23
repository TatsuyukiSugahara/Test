#pragma once
#include <memory>
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderTargetHandle.h"
#include "Graphics/IShader.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * ディファードライティングパス: フルスクリーントライアングルで GBuffer を読み取り
		 * シーン RT にライティング結果を書き込む。
		 *
		 * 前提: 呼び出し前にシーン RT が OMSetRenderTargetWithDepth でバインド済みであること。
		 * GBuffer SRV は t8-t11 にバインドし、描画後に解除する。
		 * GBuffer RT は Execute() 時に GraphicsDevice::GetRenderTarget() で解決するため、
		 * コマンド生成時点のポインタ失効を防げる。
		 */
		class DeferredLightingCommand final : public IRenderCommand
		{
		public:
			DeferredLightingCommand(
				graphics::IShader& lightingVS,
				graphics::IShader& lightingPS,
				RenderTargetHandle  gbuffer0Handle,
				RenderTargetHandle  gbuffer1Handle,
				RenderTargetHandle  gbuffer2Handle,
				RenderTargetHandle  gbuffer3Handle,
				const CameraData&  camera);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			graphics::IShader* lightingVS_;
			graphics::IShader* lightingPS_;
			RenderTargetHandle  gbuffer0Handle_;
			RenderTargetHandle  gbuffer1Handle_;
			RenderTargetHandle  gbuffer2Handle_;
			RenderTargetHandle  gbuffer3Handle_;
			CameraData          camera_;
		};
	}
}
