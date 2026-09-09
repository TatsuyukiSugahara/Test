#pragma once
#include <cstdint>
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderTargetHandle.h"
#include "PostProcessCB.h"


namespace aq
{
	namespace graphics
	{
		class IShader;
		class IConstantBuffer;
	}


	namespace rendering
	{
		/**
		 * シーンとブルームの合成 + 露出 + トーンマップ + ガンマの CS パス。
		 * HDR の 2 入力を受け取り、LDR の最終 RT へ書き出す。
		 */
		class TonemapPassCommand final : public IRenderCommand
		{
		private:
			graphics::IShader*         compositeShader_;
			graphics::IConstantBuffer* bloomCB_;

			RenderTargetHandle sceneRTHandle_;
			RenderTargetHandle bloomRTHandle_;
			RenderTargetHandle finalRTHandle_;

			float    intensity_;
			uint32_t width_;
			uint32_t height_;

			float    exposure_;
			uint32_t tonemapMode_;
			float    whitePoint_;
			uint32_t applyGamma_;


		public:
			TonemapPassCommand(
				graphics::IShader*         compositeShader,
				graphics::IConstantBuffer* bloomCB,
				RenderTargetHandle         sceneRT,
				RenderTargetHandle         bloomRT,
				RenderTargetHandle         finalRT,
				float                      intensity,
				uint32_t                   width,
				uint32_t                   height,
				float                      exposure,
				uint32_t                   tonemapMode,
				float                      whitePoint,
				uint32_t                   applyGamma);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
