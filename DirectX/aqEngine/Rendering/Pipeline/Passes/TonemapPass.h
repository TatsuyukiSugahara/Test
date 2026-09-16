#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/PostProcess/Effects/TonemapEffect.h"


namespace aq
{
	namespace rendering
	{
		class BloomPass;


		/**
		 * トーンマップのパス(設計書/レンダーパイプライン設計.md §2、P2)。
		 * TonemapEffect を薄く包む。入力は PostInput(あれば)、無ければ Scene。
		 * BloomTexture が登録されていれば合成し、無ければブルーム寄与 0 で合成する。
		 * Build の最後に SetRenderTargetCommand(Output) を積む(UI が描けるように)。
		 */
		class TonemapPass final : public IRenderPass
		{
		private:
			std::unique_ptr<TonemapEffect> effect_;

			/** ブルーム強度の取得元。BloomTexture 登録時にここから GetIntensity() を読む(nullptr なら 0) */
			const BloomPass* bloomSource_ = nullptr;


		public:
			TonemapPass();
			~TonemapPass() override;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName()  const override { return "TonemapPass"; }
			inline PassScope    GetScope() const override { return PassScope::Frame; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;
			bool Setup(PassResources& res, const uint32_t width, const uint32_t height) override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/**
			 * 配線
			 */
		public:
			/** ブルーム強度の取得元を設定する。PipelinePresets が Standard() 組み立て時に繋ぐ */
			inline void SetBloomIntensitySource(const BloomPass* bloom) { bloomSource_ = bloom; }
			inline TonemapEffect* GetEffect() const { return effect_.get(); }
		};
	}
}
