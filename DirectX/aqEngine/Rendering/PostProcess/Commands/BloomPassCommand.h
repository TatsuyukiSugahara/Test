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
		class ISamplerState;
	}


	namespace rendering
	{
		/**
		 * ブルームの生成パス（Dual Blur）を 1 コマンドにまとめた CS パス。
		 * 実行順: 輝度抽出 → Down × N → UpAccum × (N-1) → Up(最終)。
		 * 結果は brightRT に残り、合成は TonemapPassCommand が行う。
		 */
		class BloomPassCommand final : public IRenderCommand
		{
		public:
			/** ブルームピラミッドの最大段数 */
			static constexpr uint32_t MAX_LEVELS = 4;


		private:
			graphics::IShader*         extractShader_;
			graphics::IShader*         dualDownShader_;
			graphics::IShader*         dualUpShader_;
			graphics::IShader*         dualUpAccumShader_;
			graphics::ISamplerState*   sampler_;
			graphics::IConstantBuffer* bloomCB_;

			RenderTargetHandle sceneRTHandle_;
			RenderTargetHandle brightRTHandle_;
			RenderTargetHandle pyramidRTHandles_[MAX_LEVELS];

			float    threshold_;
			float    intensity_;
			uint32_t blurPasses_;
			uint32_t width_;
			uint32_t height_;


		public:
			BloomPassCommand(
				graphics::IShader*                        extractShader,
				graphics::IShader*                        dualDownShader,
				graphics::IShader*                        dualUpShader,
				graphics::IShader*                        dualUpAccumShader,
				graphics::ISamplerState*                  sampler,
				graphics::IConstantBuffer*                bloomCB,
				RenderTargetHandle                        sceneRT,
				RenderTargetHandle                        brightRT,
				const RenderTargetHandle (&pyramidRTs)[MAX_LEVELS],
				float                                     threshold,
				float                                     intensity,
				uint32_t                                  blurPasses,
				uint32_t                                  width,
				uint32_t                                  height);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
