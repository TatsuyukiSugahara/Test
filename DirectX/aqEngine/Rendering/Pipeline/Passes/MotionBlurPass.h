#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/PostProcess/Effects/MotionBlurEffect.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * モーションブラーのパス(設計書/レンダーパイプライン設計.md §2、P2)。
		 * MotionBlurEffect を薄く包む。実行したときだけ自分の出力を PostInput に書き、
		 * しなかったら Scene をそのまま PostInput に流す(実行時に決まるため Build 内で判定する)。
		 */
		class MotionBlurPass final : public IRenderPass
		{
		private:
			std::unique_ptr<MotionBlurEffect> effect_;


		public:
			MotionBlurPass();
			~MotionBlurPass() override;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName()  const override { return "MotionBlurPass"; }
			inline PassScope    GetScope() const override { return PassScope::Frame; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;
			bool Setup(PassResources& res, const uint32_t width, const uint32_t height) override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/** ゲームがモーションブラー強度を触るための入口。 */
		public:
			inline void SetStrength(const float strength) { if (effect_) { effect_->SetStrength(strength); } }
			inline MotionBlurEffect* GetEffect() const { return effect_.get(); }
		};
	}
}
