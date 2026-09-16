#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/Deferred/DeferredRenderer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * ディファードライティングパス(設計書/レンダーパイプライン設計.md §2)。
		 * DeferredRenderer::BuildLightingCommandList を薄く包み、シーン RT に書き込む。
		 * GBufferPass / DecalPass と同じ DeferredRenderer を shared_ptr で共有する。
		 */
		class DeferredLightingPass final : public IRenderPass
		{
		private:
			std::shared_ptr<DeferredRenderer> deferred_;


		public:
			explicit DeferredLightingPass(std::shared_ptr<DeferredRenderer> deferred);


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "DeferredLightingPass"; }

			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;
		};
	}
}
