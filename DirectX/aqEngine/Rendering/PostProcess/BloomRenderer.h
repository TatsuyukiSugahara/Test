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

			// トーンマップ演算子。BloomComposite.fx の Tonemap.fx と一致させること。
			enum class TonemapMode : uint32_t
			{
				None      = 0,  // クランプのみ (saturate)
				Reinhard  = 1,  // c / (1+c)
				ReinhardExt = 2, // ホワイトポイント付き Reinhard
				ACES      = 3,  // ACES (Narkowicz) フィルミック
				Uncharted2 = 4, // Uncharted2 (Hable) フィルミック
			};

			bool Initialize(uint32_t width, uint32_t height,
			                float    threshold  = 1.0f,   // HDR: 1.0 超の輝度のみブルーム
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

			// --- トーンマップ (HDR → LDR) ---
			TonemapMode GetTonemapMode() const { return tonemapMode_; }
			float       GetExposure()    const { return exposure_; }
			float       GetWhitePoint()  const { return whitePoint_; }
			bool        GetApplyGamma()  const { return applyGamma_; }
			void        SetTonemapMode(TonemapMode v) { tonemapMode_ = v; }
			void        SetExposure(float v)          { exposure_   = v; }
			void        SetWhitePoint(float v)        { whitePoint_ = v; }
			void        SetApplyGamma(bool v)         { applyGamma_ = v; }

			/** 輝度抽出後テクスチャのハンドル（デバッグ表示用）。 */
			RenderTargetHandle GetBrightRTHandle() const { return brightRTHandle_; }

#ifdef AQ_DEBUG_IMGUI
			std::unique_ptr<IDebugRenderable> CreateDebugPanel() override;
#endif

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

			// トーンマップパラメータ (HDR シーン → LDR 表示)
			TonemapMode tonemapMode_ = TonemapMode::ACES;
			float       exposure_    = 1.0f;   // 露出倍率 (トーンマップ前に乗算)
			float       whitePoint_  = 4.0f;   // ReinhardExt 用の白飛びポイント
			bool        applyGamma_  = false;  // ガンマ空間パイプラインのため既定 off
		};
	}
}
