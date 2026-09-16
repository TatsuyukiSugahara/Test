#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/Shadow/IShadowRenderer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * シャドウパス(設計書/レンダーパイプライン設計.md §2)。
		 * IShadowRenderer を薄く包み、シャドウ定数バッファの計算とシャドウマップの描画を委譲する。
		 * シャドウマップ自体は PassResources ではなく FrameContext 経由で描画コマンドに渡る。
		 */
		class ShadowPass final : public IRenderPass
		{
		private:
			std::unique_ptr<IShadowRenderer> renderer_;


		public:
			explicit ShadowPass(std::unique_ptr<IShadowRenderer> renderer);


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName()  const override { return "ShadowPass"; }
			inline PassScope    GetScope() const override { return PassScope::Frame; }

			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/** ゲームがシャドウ設定(ImGui 等)を触るための入口。AquaDash が使用(設計書 §2)。 */
		public:
			inline IShadowRenderer* GetShadowRenderer() const { return renderer_.get(); }
		};
	}
}
