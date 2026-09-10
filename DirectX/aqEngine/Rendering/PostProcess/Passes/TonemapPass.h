#pragma once
#include <memory>
#include "Rendering/PostProcess/IPostProcessPass.h"
#include "Rendering/RenderTargetHandle.h"
#include "Graphics/IShader.h"
#include "Graphics/IBuffer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * ブルーム合成 + 露出 + トーンマップ + ガンマのパス。
		 * HDR のシーンとブルームを合成して LDR の最終 RT を作る。
		 * HDR → LDR 変換が無いと表示できないため常時有効。
		 */
		class TonemapPass final : public IPostProcessPass
		{
		public:
			/** トーンマップ演算子。BloomComposite.fx の Tonemap.fx と一致させること。 */
			enum class TonemapMode : uint32_t
			{
				None        = 0,  // クランプのみ (saturate)
				Reinhard    = 1,  // c / (1+c)
				ReinhardExt = 2,  // ホワイトポイント付き Reinhard
				ACES        = 3,  // ACES (Narkowicz) フィルミック
				Uncharted2  = 4,  // Uncharted2 (Hable) フィルミック
			};


		private:
			std::unique_ptr<graphics::IShader>         compositeShader_;
			std::unique_ptr<graphics::IConstantBuffer> bloomCB_;

			/** トーンマップ後の LDR 出力 */
			RenderTargetHandle finalRTHandle_;

			/** 合成に使うブルーム入力 (チェーンが Build 前に渡す) */
			RenderTargetHandle bloomRTHandle_;
			float              bloomIntensity_ = 0.0f;

			/** トーンマップパラメータ (HDR シーン → LDR 表示) */
			TonemapMode tonemapMode_ = TonemapMode::ACES;
			float       exposure_    = 1.0f;   // 露出倍率 (トーンマップ前に乗算)
			float       whitePoint_  = 4.0f;   // ReinhardExt 用の白飛びポイント
			bool        applyGamma_  = false;  // ガンマ空間パイプラインのため既定 off


		public:
			TonemapPass() = default;
			~TonemapPass() override = default;


		public:
			bool Initialize(const uint32_t width, const uint32_t height) override;
			bool IsEnabled(const PostProcessContext& context) const override;

			RenderTargetHandle Build(
				RenderCommandList&        outList,
				const PostProcessContext& context,
				const RenderTargetHandle  input) override;

			/**
			 * 合成するブルームを設定する
			 * @param handle    ブルーム結果の RT
			 * @param intensity ブルームの合成強度 (0 で寄与なし)
			 */
			inline void SetBloomInput(const RenderTargetHandle handle, const float intensity)
			{
				bloomRTHandle_  = handle;
				bloomIntensity_ = intensity;
			}

			/** トーンマップ後の最終 RT */
			inline RenderTargetHandle GetFinalRT() const { return finalRTHandle_; }


			/**
			 * パラメータ
			 */
		public:
			inline TonemapMode GetTonemapMode() const { return tonemapMode_; }
			inline float       GetExposure()    const { return exposure_; }
			inline float       GetWhitePoint()  const { return whitePoint_; }
			inline bool        GetApplyGamma()  const { return applyGamma_; }
			inline void        SetTonemapMode(const TonemapMode v) { tonemapMode_ = v; }
			inline void        SetExposure(const float v)          { exposure_   = v; }
			inline void        SetWhitePoint(const float v)        { whitePoint_ = v; }
			inline void        SetApplyGamma(const bool v)         { applyGamma_ = v; }
		};
	}
}
