#pragma once
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderTargetHandle.h"
#include "BloomRenderer.h"

namespace aq
{
	namespace graphics
	{
		class IShader;
		class IConstantBuffer;
		class ISamplerState;
	}

	namespace rendering
	{
		struct BloomCBData
		{
			float    threshold;
			float    intensity;
			uint32_t width;
			uint32_t height;
			uint32_t isVertical;
			uint32_t pad[3];
		};

		/**
		 * Bloom 全パスを 1 コマンドにまとめた CS パス（Dual Blur）。
		 * 実行順: 輝度抽出 → Down × N → UpAccum × (N-1) → Up(最終) → 合成
		 */
		class BloomPassCommand final : public IRenderCommand
		{
		public:
			BloomPassCommand(
				graphics::IShader*                                    extractShader,
				graphics::IShader*                                    dualDownShader,
				graphics::IShader*                                    dualUpShader,
				graphics::IShader*                                    dualUpAccumShader,
				graphics::IShader*                                    compositeShader,
				graphics::ISamplerState*                              sampler,
				graphics::IConstantBuffer*                            bloomCB,
				RenderTargetHandle                                    sceneRT,
				RenderTargetHandle                                    brightRT,
				const RenderTargetHandle (&pyramidRTs)[BloomRenderer::kMaxLevels],
				RenderTargetHandle                                    finalRT,
				float                                                 threshold,
				float                                                 intensity,
				uint32_t                                              blurPasses,
				uint32_t                                              width,
				uint32_t                                              height);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			graphics::IShader*         extractShader_;
			graphics::IShader*         dualDownShader_;
			graphics::IShader*         dualUpShader_;
			graphics::IShader*         dualUpAccumShader_;
			graphics::IShader*         compositeShader_;
			graphics::ISamplerState*   sampler_;
			graphics::IConstantBuffer* bloomCB_;

			RenderTargetHandle sceneRTHandle_;
			RenderTargetHandle brightRTHandle_;
			RenderTargetHandle pyramidRTHandles_[BloomRenderer::kMaxLevels];
			RenderTargetHandle finalRTHandle_;

			float    threshold_;
			float    intensity_;
			uint32_t blurPasses_;
			uint32_t width_;
			uint32_t height_;
		};
	}
}
