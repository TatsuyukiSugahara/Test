#pragma once
#include <memory>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/Occlusion/HiZRenderer.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * Hi-Z ピラミッド構築パス(設計書/レンダーパイプライン設計.md §2)。
		 * HiZRenderer を薄く包む。単一カメラ前提のため分割画面では動かない(IsSingleView() で判定)。
		 */
		class HiZPass final : public IRenderPass
		{
		private:
			std::shared_ptr<HiZRenderer> hiZ_;


		public:
			explicit HiZPass(std::shared_ptr<HiZRenderer> hiZ);


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "HiZPass"; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;
			bool Setup(PassResources& res, const uint32_t width, const uint32_t height) override;
			inline ResizePolicy GetResizePolicy() const override { return ResizePolicy::Resizable; }
			bool PrepareResize(const uint32_t width, const uint32_t height) override;
			void CommitResize(PassResources& res) override;
			void AbortResize() override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


		private:
			/** Hi-Z のミップ連鎖の生成(Setup 用) */
			bool CreateTargets(const uint32_t width, const uint32_t height);
			/** Hi-Z(CPU リードバック用ハンドル)を掲示板へ登録する(Setup と CommitResize で共有) */
			void Wire(PassResources& res);


			/** ゲームが Hi-Z に直接触るための入口(オクリュージョンテスタ登録等)。 */
		public:
			inline HiZRenderer* GetHiZRenderer() const { return hiZ_.get(); }
		};
	}
}
