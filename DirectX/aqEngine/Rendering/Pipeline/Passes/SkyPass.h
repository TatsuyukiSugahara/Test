#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/Sky/SkyRenderer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * スカイパス(設計書/レンダーパイプライン設計.md §2)。
		 * SkyRenderer を薄く包む。ディファードライティング直後・フォワード直前に置く想定
		 * (フォワード構成では Depth が無いので深度なしで描く)。
		 */
		class SkyPass final : public IRenderPass
		{
		private:
			std::unique_ptr<SkyRenderer> sky_;


		public:
			explicit SkyPass(std::unique_ptr<SkyRenderer> sky);


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "SkyPass"; }

			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/** ゲームが空の色調 (Tint) 等を触るための入口。 */
		public:
			inline SkyRenderer* GetSkyRenderer() const { return sky_.get(); }
		};
	}
}
