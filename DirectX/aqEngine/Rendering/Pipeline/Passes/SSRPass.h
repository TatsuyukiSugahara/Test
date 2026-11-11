#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/Deferred/DeferredRenderer.h"
#include "Rendering/SSR/SSRRenderer.h"


namespace aq
{
	namespace rendering
	{
		class IBLEnvironment;


		/**
		 * SSR パス(設計書/SSR設計.md §1)。
		 * SSRRenderer を包む。DeferredLightingPass の直後・SkyPass の前に置く(Standard のみ)。
		 * GBufferPass / DecalPass / DeferredLightingPass と同じ DeferredRenderer、ライティングと同じ IBLEnvironment を
		 * shared_ptr で共有する。
		 *
		 * SSR が有効、かつ同じビューのライティングが IBL(PSMainIBL)で描いた(DeferredRenderer::WasIBLUsed)ビューでだけ
		 * コマンドを積む。IBL が使えない間はライティングに鏡面キューブの項が無く、差し引くと誤るため(§0.2)。
		 */
		class SSRPass final : public IRenderPass
		{
		private:
			std::unique_ptr<SSRRenderer>      ssr_;
			std::shared_ptr<DeferredRenderer> deferred_;
			std::shared_ptr<IBLEnvironment>   ibl_;


		public:
			SSRPass(std::unique_ptr<SSRRenderer>      ssr,
			        std::shared_ptr<DeferredRenderer> deferred,
			        std::shared_ptr<IBLEnvironment>   ibl);
			~SSRPass() override;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "SSRPass"; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;

			/**
			 * リサイズ。トレースの出力 RT(画面寸法)を持つので Resizable(設計書 §1)。
			 * P1 ではまだ RT を持たないので、PrepareResize / CommitResize / AbortResize は既定の何もしない実装のまま。
			 * P2 で出力 RT を足すときに SSRRenderer へ委ねる。
			 */
			inline ResizePolicy GetResizePolicy() const override { return ResizePolicy::Resizable; }

			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/** ゲームが SSR の ON/OFF・testMode 等を触るための入口。 */
		public:
			inline SSRRenderer* GetSSRRenderer() const { return ssr_.get(); }
		};
	}
}
