#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/Deferred/DeferredRenderer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * G-Buffer パス(設計書/レンダーパイプライン設計.md §2)。
		 * DeferredRenderer::BuildGBufferCommandList を薄く包む。
		 * DecalPass / DeferredLightingPass と同じ DeferredRenderer を shared_ptr で共有する。
		 */
		class GBufferPass final : public IRenderPass
		{
		private:
			std::shared_ptr<DeferredRenderer> deferred_;


		public:
			explicit GBufferPass(std::shared_ptr<DeferredRenderer> deferred);


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "GBufferPass"; }

			void DeclareResources(PassDeclaration& decl) const override;
			bool Setup(PassResources& res, const uint32_t width, const uint32_t height) override;
			inline ResizePolicy GetResizePolicy() const override { return ResizePolicy::Resizable; }
			bool PrepareResize(const uint32_t width, const uint32_t height) override;
			void CommitResize(PassResources& res) override;
			void AbortResize() override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/** G-Buffer の生成(Setup 用。既に生成済みなら何もしない)。失敗したら false */
		private:
			bool CreateTargets(const uint32_t width, const uint32_t height);
			/** G-Buffer を掲示板へ登録する(Setup と CommitResize で共有) */
			void Wire(PassResources& res);


			/** ゲームが DeferredRenderer に直接触るための入口。 */
		public:
			inline DeferredRenderer* GetDeferredRenderer() const { return deferred_.get(); }
		};
	}
}
