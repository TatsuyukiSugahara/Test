#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/PostProcess/IPostProcessRenderer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * ポストプロセスパス(設計書/レンダーパイプライン設計.md §2、P1 のみ)。
		 * IPostProcessRenderer(P1 では PostProcessChain 丸ごと)を薄く包む。
		 * P2 で MotionBlurPass / BloomPass / TonemapPass に分割される。
		 */
		class PostProcessPass final : public IRenderPass
		{
		private:
			std::unique_ptr<IPostProcessRenderer> pp_;


		public:
			explicit PostProcessPass(std::unique_ptr<IPostProcessRenderer> pp);


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName()  const override { return "PostProcessPass"; }
			inline PassScope    GetScope() const override { return PassScope::Frame; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;
			bool Setup(PassResources& res, const uint32_t width, const uint32_t height) override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/** ゲームがモーションブラー強度等を触るための入口。 */
		public:
			inline IPostProcessRenderer* GetPostProcessRenderer() const { return pp_.get(); }
		};
	}
}
