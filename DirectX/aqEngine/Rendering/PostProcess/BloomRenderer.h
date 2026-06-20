#pragma once
#include <memory>
#include "IPostProcessRenderer.h"
#include "Rendering/RenderTargetHandle.h"
#include "Graphics/IShader.h"
#include "Graphics/IBuffer.h"
#include "Graphics/ISamplerState.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * CS ベースの Bloom（Dual Blur アルゴリズム）。
		 * BrightExtract → Down × N → Up × N → Composite の順で実行する。
		 *
		 * 使い方:
		 *   auto bloom = std::make_unique<BloomRenderer>();
		 *   bloom->Initialize(width, height);
		 *   renderer.SetPostProcessRenderer(std::move(bloom));
		 */
		class BloomRenderer : public IPostProcessRenderer
		{
		public:
			static constexpr uint32_t kMaxLevels = 4;

			BloomRenderer() = default;
			~BloomRenderer() override = default;

			bool Initialize(uint32_t width, uint32_t height,
			                float    threshold  = 0.7f,
			                float    intensity  = 0.45f,
			                uint32_t blurPasses = 4);

			void BuildPostProcessCommandList(
				RenderCommandList& outList,
				RenderTargetHandle sceneRT,
				uint32_t           width,
				uint32_t           height) const override;

			RenderTargetHandle GetFinalRT() const override { return finalRTHandle_; }

			float    GetThreshold()  const { return threshold_; }
			float    GetIntensity()  const { return intensity_; }
			uint32_t GetBlurPasses() const { return blurPasses_; }
			void     SetThreshold(float v)     { threshold_  = v; }
			void     SetIntensity(float v)     { intensity_  = v; }
			void     SetBlurPasses(uint32_t v) { blurPasses_ = (v < 1) ? 1 : (v > kMaxLevels) ? kMaxLevels : v; }

		private:
			std::unique_ptr<graphics::IShader>         extractShader_;
			std::unique_ptr<graphics::IShader>         dualBlurDownShader_;
			std::unique_ptr<graphics::IShader>         dualBlurUpShader_;       // 最終段（brightRT への純粋上書き）
			std::unique_ptr<graphics::IShader>         dualBlurUpAccumShader_;  // 中間段（pyramid への累積加算）
			std::unique_ptr<graphics::IShader>         compositeShader_;
			std::unique_ptr<graphics::IConstantBuffer> bloomCB_;
			std::unique_ptr<graphics::ISamplerState>   sampler_;

			RenderTargetHandle brightRTHandle_;
			RenderTargetHandle pyramidRTHandles_[kMaxLevels]; // pyramid[i]: W/2^(i+1) × H/2^(i+1)
			RenderTargetHandle finalRTHandle_;

			float    threshold_  = 0.5f;
			float    intensity_  = 2.0f;
			uint32_t blurPasses_ = 3;
			uint32_t width_      = 0;
			uint32_t height_     = 0;
		};
	}
}
