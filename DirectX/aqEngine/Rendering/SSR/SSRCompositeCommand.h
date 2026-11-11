#pragma once
#include <memory>
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderTargetHandle.h"
#include "Math/Vector.h"
#include "Graphics/IShader.h"


namespace aq
{
	namespace rendering
	{
		class IBLEnvironment;


		/**
		 * SSR 合成用 定数バッファ (b2)。
		 * perDrawCBPool (= sizeof(VSConstantBuffer) = 192B) から Allocate するため 192B に固定する
		 * (IBLCBData / SkyCBData と同じ作法)。SSRComposite.fx の cbuffer SSRCompositeCB と先頭を一致させること。
		 */
		struct SSRCompositeCBData
		{
			float         iblIntensity      = 1.0f;  // ライティングが鏡面キューブに掛けた強度 (同じ値で差し引く)
			float         iblSpecularMaxMip = 5.0f;  // 鏡面キューブのミップ数 - 1
			float         _pad0             = 0.0f;
			float         _pad1             = 0.0f;
			/** P1 のテスト用 (SSRRenderer の testMode)。ssr (当たった色 rgb + 確信度 a) の固定値。P2 でトレースの出力 RT に置き換える */
			math::Vector4 ssrTestValue      = math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
			math::Vector4 pad[10];                   // 192B へのパディング
		};
		static_assert(sizeof(SSRCompositeCBData) == 192,
		              "SSRCompositeCBData must match perDrawCBPool slot size (192B)");




		/**
		 * SSR の差分合成 (設計書/SSR設計.md §0.2・§1)。
		 * フルスクリーン三角形 1 枚で、G-Buffer の画素にだけ
		 * `ssr.a × (ssr.rgb − 鏡面キューブ × iblIntensity) × (F0·A + B)` を加算する (BlendMode::Additive、α = 1)。
		 *
		 * 前提: 呼び出し前にシーン RT がバインド済みであること (このコマンドは RT を設定しない)。
		 * 深度は無効で描き、描画後は BlendMode を Opaque、DepthMode を ReadWrite へ戻す (パス契約 2)。
		 *
		 * G-Buffer はハンドルで持ち Execute() で解決する (DeferredLightingCommand / MotionBlurPassCommand と同じ)。
		 * IBL の環境は shared_ptr で持つ (Execute はレンダースレッドで後のフレームに走るため、
		 * 実行待ちの間も資源を生かす。設計書/IBL設計.md §1.1)。強度と testMode の値はコマンド生成時に写す。
		 * CB は Execute 内で fc.perDrawCBPool から取る。
		 */
		class SSRCompositeCommand final : public IRenderCommand
		{
		private:
			graphics::IShader* compositeVS_;
			graphics::IShader* compositePS_;

			/** G-Buffer (Execute 時に解決する) */
			RenderTargetHandle gbuffer0Handle_;
			RenderTargetHandle gbuffer1Handle_;
			RenderTargetHandle gbuffer2Handle_;
			RenderTargetHandle gbuffer3Handle_;

			/** IBL の環境と、コマンド生成時の強度 */
			std::shared_ptr<const IBLEnvironment> ibl_;
			float                                 iblIntensity_;

			/** P1 のテスト用の ssr の固定値 (SSRCompositeCBData::ssrTestValue) */
			math::Vector4 ssrTestValue_;


		public:
			SSRCompositeCommand(
				graphics::IShader&                    compositeVS,
				graphics::IShader&                    compositePS,
				RenderTargetHandle                    gbuffer0Handle,
				RenderTargetHandle                    gbuffer1Handle,
				RenderTargetHandle                    gbuffer2Handle,
				RenderTargetHandle                    gbuffer3Handle,
				std::shared_ptr<const IBLEnvironment> ibl,
				const float                           iblIntensity,
				const math::Vector4&                  ssrTestValue);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
