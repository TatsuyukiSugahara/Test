#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/Deferred/DeferredRenderer.h"
#include "Rendering/SSAO/SSAORenderer.h"


namespace aq
{
	namespace rendering
	{
		class IBLEnvironment;


		/**
		 * SSAO パス(設計書/SSAO設計.md §1)。
		 * SSAORenderer を包む。DeferredLightingPass の直後・SSRPass の前に置く(Standard のみ)。
		 * GBufferPass / DeferredLightingPass と同じ DeferredRenderer、ライティングと同じ IBLEnvironment(null 可)を
		 * shared_ptr で共有する。
		 *
		 * SSAO が有効なビューでだけコマンドを積む。拡散の環境光の差し引きは、同じビューのライティングが
		 * IBL(PSMainIBL)で描いたか(DeferredRenderer::WasIBLUsed)でエントリを選ぶ(§0.2)。
		 * 積む順は AO の計算(compute。P2)→ シーン RT のバインド → 合成。P1 では compute を持たない。
		 */
		class SSAOPass final : public IRenderPass
		{
		private:
			std::unique_ptr<SSAORenderer>     ssao_;
			std::shared_ptr<DeferredRenderer> deferred_;
			std::shared_ptr<IBLEnvironment>   ibl_;


		public:
			SSAOPass(std::unique_ptr<SSAORenderer>     ssao,
			         std::shared_ptr<DeferredRenderer> deferred,
			         std::shared_ptr<IBLEnvironment>   ibl);
			~SSAOPass() override;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "SSAOPass"; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;

			/**
			 * AO の RT を作る。作れなくても false は返さない
			 * (Setup の失敗はパイプライン全体の失敗になるため。SSAO は何も積まず、SSAO なしの絵で続行する)。
			 */
			bool Setup(PassResources& res, const uint32_t width, const uint32_t height) override;

			/**
			 * リサイズ。AO の RT(画面寸法)を持つので Resizable。SSAORenderer へ委ねる。
			 * 掲示板には何も登録しないので、CommitResize で取り直す入力は無い。
			 */
			inline ResizePolicy GetResizePolicy() const override { return ResizePolicy::Resizable; }
			bool PrepareResize(const uint32_t width, const uint32_t height) override;
			void CommitResize(PassResources& res) override;
			void AbortResize() override;

			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/** ゲームが SSAO の ON/OFF・パラメータ・デバッグ表示・testMode を触るための入口。 */
		public:
			inline SSAORenderer* GetSSAORenderer() const { return ssao_.get(); }
		};
	}
}
