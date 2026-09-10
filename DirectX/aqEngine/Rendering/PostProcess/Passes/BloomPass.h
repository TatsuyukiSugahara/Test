#pragma once
#include <memory>
#include "Rendering/PostProcess/IPostProcessPass.h"
#include "Rendering/PostProcess/Commands/BloomPassCommand.h"
#include "Rendering/RenderTargetHandle.h"
#include "Graphics/IShader.h"
#include "Graphics/IBuffer.h"
#include "Graphics/ISamplerState.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * CS ベースのブルーム生成パス（Dual Blur アルゴリズム）。
		 * 輝度抽出 → Down × N → Up × N を行い、結果を bright RT に残す。
		 * 合成は TonemapPass が担当する。
		 */
		class BloomPass final : public IPostProcessPass
		{
		public:
			/** ブルームピラミッドの最大段数 */
			static constexpr uint32_t MAX_LEVELS = BloomPassCommand::MAX_LEVELS;


		private:
			std::unique_ptr<graphics::IShader>         extractShader_;
			std::unique_ptr<graphics::IShader>         dualBlurDownShader_;
			std::unique_ptr<graphics::IShader>         dualBlurUpShader_;       // 最終段（brightRT への純粋上書き）
			std::unique_ptr<graphics::IShader>         dualBlurUpAccumShader_;  // 中間段（pyramid への累積加算）
			std::unique_ptr<graphics::IConstantBuffer> bloomCB_;
			std::unique_ptr<graphics::ISamplerState>   sampler_;

			RenderTargetHandle brightRTHandle_;
			RenderTargetHandle pyramidRTHandles_[MAX_LEVELS]; // pyramid[i]: W/2^(i+1) × H/2^(i+1)

			float    threshold_  = 1.0f;   // HDR: 1.0 超の輝度のみブルーム
			float    intensity_  = 0.45f;
			uint32_t blurPasses_ = MAX_LEVELS;


		public:
			BloomPass() = default;
			~BloomPass() override = default;


		public:
			bool Initialize(const uint32_t width, const uint32_t height) override;
			bool IsEnabled(const PostProcessContext& context) const override;

			RenderTargetHandle Build(
				RenderCommandList&        outList,
				const PostProcessContext& context,
				const RenderTargetHandle  input) override;

			/** 輝度抽出後テクスチャのハンドル（デバッグ表示用） */
			inline RenderTargetHandle GetBrightRTHandle() const { return brightRTHandle_; }


			/**
			 * パラメータ
			 */
		public:
			inline float    GetThreshold()  const { return threshold_; }
			inline float    GetIntensity()  const { return intensity_; }
			inline uint32_t GetBlurPasses() const { return blurPasses_; }
			inline void     SetThreshold(const float v)     { threshold_  = v; }
			inline void     SetIntensity(const float v)     { intensity_  = v; }
			inline void     SetBlurPasses(const uint32_t v) { blurPasses_ = (v < 1) ? 1 : (v > MAX_LEVELS) ? MAX_LEVELS : v; }
		};
	}
}
