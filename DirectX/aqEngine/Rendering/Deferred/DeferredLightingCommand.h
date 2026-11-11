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
		class IBLEnvironment;


		/**
		 * ディファードライティングパス: フルスクリーントライアングルで GBuffer を読み取り
		 * シーン RT にライティング結果を書き込む。
		 *
		 * 前提: 呼び出し前にシーン RT が OMSetRenderTargetWithDepth でバインド済みであること。
		 * GBuffer SRV は t8-t11 にバインドし、描画後に解除する。
		 * GBuffer RT は Execute() 時に GraphicsDevice::GetRenderTarget() で解決するため、
		 * コマンド生成時点のポインタ失効を防げる。
		 *
		 * IBL(設計書/IBL設計.md §1)を使うときは ibl に環境を渡し、lightingPS に PSMainIBL を渡す。
		 * b2 に IBLCB(perDrawCBPool から取る)、t5-t7 に irradiance / 鏡面 / BRDF LUT、s0 にサンプラを
		 * バインドし、描画後に t5-t7 を解除する。環境は shared_ptr で保持する
		 * (Execute はレンダースレッドで後のフレームに走るため、実行待ちの間も資源を生かす。§1.1)。
		 * ibl が null のときは従来どおり(PSMain)で、t5-t7 / s0 / b2 に触れない。
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
				const CameraData&  camera,
				std::shared_ptr<const IBLEnvironment> ibl          = nullptr,
				const float                           iblIntensity = 1.0f);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			graphics::IShader* lightingVS_;
			graphics::IShader* lightingPS_;
			RenderTargetHandle  gbuffer0Handle_;
			RenderTargetHandle  gbuffer1Handle_;
			RenderTargetHandle  gbuffer2Handle_;
			RenderTargetHandle  gbuffer3Handle_;
			CameraData          camera_;

			/** IBL(使わないときは null)。強度はコマンド生成時の値を写す */
			std::shared_ptr<const IBLEnvironment> ibl_;
			float                                 iblIntensity_;
		};
	}
}
