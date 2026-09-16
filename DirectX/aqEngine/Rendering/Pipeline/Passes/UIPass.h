#pragma once
#include <functional>
#include "Rendering/Pipeline/IRenderPass.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * UI パス(設計書/レンダーパイプライン設計.md §2)。
		 * UIBatchRenderer::BuildCommandList を今どおりコールバック経由で薄く包む
		 * (エンジンが UI モジュールを直接知らないため)。
		 */
		class UIPass final : public IRenderPass
		{
		private:
			std::function<void(RenderCommandList&)> callback_;


		public:
			explicit UIPass(std::function<void(RenderCommandList&)> callback);


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName()  const override { return "UIPass"; }
			inline PassScope    GetScope() const override { return PassScope::Frame; }

			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;
		};
	}
}
