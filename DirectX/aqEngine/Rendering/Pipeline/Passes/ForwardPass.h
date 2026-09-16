#pragma once
#include "Rendering/Pipeline/IRenderPass.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * フォワードパス(設計書/レンダーパイプライン設計.md §2)。
		 * Renderer::RecordDrawItem + recordForwardItems + InstancedDrawItemCommand を移したもの。
		 * GBufferPass が無い構成(フォワードのみ)では frame.items(deferred 用アイテム)も
		 * ここでまとめて描く。
		 */
		class ForwardPass final : public IRenderPass
		{
		public:
			ForwardPass() = default;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "ForwardPass"; }

			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;
		};
	}
}
