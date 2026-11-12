#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/PostProcess/Effects/DoFEffect.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 被写界深度のパス(設計書/被写界深度設計.md)。MotionBlurPass の直後、BloomPass の前に置く。
		 * DoFEffect を薄く包む。入力は PostInput(無ければ Scene)。
		 * 有効 && frameViewCount == 1 && WorldPos があるときだけ積んで PostInput を自分の出力に、
		 * それ以外は読んだ入力を PostInput に登録する(毎フレーム必ず登録する。RenderPipeline がフレームの始めに消すため)。
		 * 積んだフレームは CoC RT を DoFCoC に登録し、積まないフレームは DoFCoC を消す(OutlinePass が読む。設計書 §0.5)。
		 */
		class DoFPass final : public IRenderPass
		{
		private:
			std::unique_ptr<DoFEffect> effect_;


		public:
			DoFPass();
			~DoFPass() override;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName()  const override { return "DoFPass"; }
			inline PassScope    GetScope() const override { return PassScope::Frame; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;
			bool Setup(PassResources& res, const uint32_t width, const uint32_t height) override;
			inline ResizePolicy GetResizePolicy() const override { return ResizePolicy::Resizable; }
			bool PrepareResize(const uint32_t width, const uint32_t height) override;
			void CommitResize(PassResources& res) override;
			void AbortResize() override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/** ゲームがパラメータを触るための入口(値の読み出しは GetEffect() の Get*) */
		public:
			inline void SetEnabled(const bool enabled)            { if (effect_) { effect_->SetEnabled(enabled); } }
			inline void SetFocusDistance(const float distance)    { if (effect_) { effect_->SetFocusDistance(distance); } }
			inline void SetFocusRange(const float range)          { if (effect_) { effect_->SetFocusRange(range); } }
			inline void SetNearTransition(const float transition) { if (effect_) { effect_->SetNearTransition(transition); } }
			inline void SetFarTransition(const float transition)  { if (effect_) { effect_->SetFarTransition(transition); } }
			inline void SetMaxRadius(const float radius)          { if (effect_) { effect_->SetMaxRadius(radius); } }
			inline void SetDebugView(const bool debugView)        { if (effect_) { effect_->SetDebugView(debugView); } }
			inline void SetAutoFocusEnabled(const bool enabled)   { if (effect_) { effect_->SetAutoFocusEnabled(enabled); } }
			inline void SetAutoFocusSpeed(const float speed)      { if (effect_) { effect_->SetAutoFocusSpeed(speed); } }
			inline DoFEffect* GetEffect() const { return effect_.get(); }
		};
	}
}
