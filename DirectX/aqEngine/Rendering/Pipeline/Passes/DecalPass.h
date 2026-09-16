#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/Deferred/DeferredRenderer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 投影デカールパス(設計書/レンダーパイプライン設計.md §2)。
		 * DeferredRenderer::BuildDecalCommandList を薄く包む。GBuffer0(albedo) へ書き戻す
		 * (ライティング前)。GBufferPass と同じ DeferredRenderer を shared_ptr で共有する。
		 */
		class DecalPass final : public IRenderPass
		{
		private:
			std::shared_ptr<DeferredRenderer> deferred_;


		public:
			explicit DecalPass(std::shared_ptr<DeferredRenderer> deferred);


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "DecalPass"; }

			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;
		};
	}
}
