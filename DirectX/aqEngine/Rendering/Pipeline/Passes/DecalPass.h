#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/Deferred/DeferredRenderer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 投影デカールパス(設計書/レンダーパイプライン設計.md §2)。
		 * DeferredRenderer::BuildDecalCommandList を薄く包む。GBuffer0(albedo) へ書き戻す
		 * (ライティング前)。GBufferPass と同じ DeferredRenderer を shared_ptr で共有する。
		 */
		class DecalPass final : public IRenderPass
		{
		private:
			std::shared_ptr<DeferredRenderer> deferred_;


		public:
			explicit DecalPass(std::shared_ptr<DeferredRenderer> deferred);


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "DecalPass"; }

			/**
			 * リサイズ(設計書/エンジン課題解消設計.md P4)。**Independent ではなく Resizable** にしている。
			 * DeclareResources で GBuffer0 を Writes している(= G-Buffer の書き戻しの順序宣言)ため、
			 * Independent の規則(Scene 以外のキーを Writes しない)に当たるが、掲示板へは何も登録しない。
			 * G-Buffer の RT は GBufferPass が持ち、同じ DeferredRenderer インスタンスの中身が入れ替わるので、
			 * このパスがするべき作業は無い(PrepareResize / CommitResize / AbortResize は既定の何もしない実装のまま)。
			 */
			inline ResizePolicy GetResizePolicy() const override { return ResizePolicy::Resizable; }

			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;
		};
	}
}
