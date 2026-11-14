#pragma once
#include <cstdint>
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
		 * SSAO 合成用 定数バッファ (b2)。
		 * perDrawCBPool (= sizeof(VSConstantBuffer) = 192B) から Allocate するため 192B に固定する
		 * (SSRCompositeCBData / IBLCBData と同じ作法)。SSAOComposite.fx の cbuffer SSAOCompositeCB と先頭を一致させること。
		 */
		struct SSAOCompositeCBData
		{
			float         iblIntensity      = 1.0f;  // ライティングが IBL に掛けた強度 (PSMainIBL が拡散を同じ強度で差し引く)
			float         iblSpecularMaxMip = 0.0f;  // 未使用 (IBLCB と並びを揃えるため)
			uint32_t      testMode          = 0;     // デバッグ用 (SSAORenderer::TEST_MODE_*)。1 は ao = 1、2 は ao = 0 に固定
			uint32_t      debugView         = 0;     // デバッグ表示 (0 以外で ao をグレーで返す)
			math::Vector4 pad[11];                   // 192B へのパディング
		};
		static_assert(sizeof(SSAOCompositeCBData) == 192,
		              "SSAOCompositeCBData must match perDrawCBPool slot size (192B)");




		/**
		 * SSAO の差分合成 (設計書/SSAO設計.md §0.2・§1)。
		 * フルスクリーン三角形 1 枚で、G-Buffer の画素にだけ `-(1 - ao) × 拡散の環境光` を加算する
		 * (BlendMode::Additive、α = 1)。ao はぼかした AO の RT を同じ画素で読む。
		 * IBL を使うビューでは PSMainIBL (EvalIBLDiffuse × 強度)、使わないビューでは PSMain (簡易 ambient) を渡す。
		 * デバッグ表示が有効なら ao をグレーで返し、BlendMode::Opaque で上書きする。
		 *
		 * 前提: 呼び出し前にシーン RT がバインド済みであること (このコマンドは RT を設定しない)。
		 * 深度は無効で描き、描画後は BlendMode を Opaque、DepthMode を ReadWrite へ戻す (パス契約 2)。
		 *
		 * G-Buffer と AO はハンドルで持ち Execute() で解決する。IBL の環境 (null 可。PSMain のとき) は
		 * shared_ptr で持つ (Execute はレンダースレッドで後のフレームに走るため、資源を生かす)。
		 * 強度・testMode・デバッグ表示の値はコマンド生成時に写す。CB は Execute 内で fc.perDrawCBPool から取る。
		 */
		class SSAOCompositeCommand final : public IRenderCommand
		{
		private:
			graphics::IShader* compositeVS_;
			graphics::IShader* compositePS_;

			/** G-Buffer (Execute 時に解決する) */
			RenderTargetHandle gbuffer0Handle_;
			RenderTargetHandle gbuffer1Handle_;
			RenderTargetHandle gbuffer2Handle_;
			RenderTargetHandle gbuffer3Handle_;

			/** ぼかした AO の RT (Execute 時に解決する) */
			RenderTargetHandle aoHandle_;

			/** IBL の環境 (PSMainIBL のときだけ。null なら IBL を使わない) と、コマンド生成時の強度 */
			std::shared_ptr<const IBLEnvironment> ibl_;
			float                                 iblIntensity_;

			/** デバッグ用の固定入力 (SSAORenderer::TEST_MODE_*) とデバッグ表示 */
			uint32_t testMode_;
			bool     debugView_;


		public:
			SSAOCompositeCommand(
				graphics::IShader&                    compositeVS,
				graphics::IShader&                    compositePS,
				RenderTargetHandle                    gbuffer0Handle,
				RenderTargetHandle                    gbuffer1Handle,
				RenderTargetHandle                    gbuffer2Handle,
				RenderTargetHandle                    gbuffer3Handle,
				RenderTargetHandle                    aoHandle,
				std::shared_ptr<const IBLEnvironment> ibl,
				const float                           iblIntensity,
				const uint32_t                        testMode,
				const bool                            debugView);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;
		};
	}
}
