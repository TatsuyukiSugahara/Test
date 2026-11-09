#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/PostProcess/Effects/BloomEffect.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * ブルームのパス(設計書/レンダーパイプライン設計.md §2、P2)。
		 * BloomEffect を薄く包む。入力は PostInput(あれば)、無ければ Scene。
		 * 無効なとき(シェーダー/RT が用意できない環境)は BloomTexture を登録しない
		 * (TonemapPass が res.Has() で判定する)。
		 */
		class BloomPass final : public IRenderPass
		{
		private:
			std::unique_ptr<BloomEffect> effect_;


		public:
			BloomPass();
			~BloomPass() override;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName()  const override { return "BloomPass"; }
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


			/** ゲームがしきい値・強度等を触るための入口。 */
		public:
			inline BloomEffect* GetEffect() const { return effect_.get(); }


		private:
			/** 掲示板への登録と入力の取り直し(Setup / CommitResize 共用)。Bloom は Build で登録するので何もしない */
			void Wire(PassResources& res);
		};
	}
}
