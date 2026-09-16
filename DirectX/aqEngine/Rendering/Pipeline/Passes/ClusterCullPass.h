#pragma once
#include "Rendering/Pipeline/IRenderPass.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * GPU 駆動クラスタ(トライアングル)カリングパス(設計書/レンダーパイプライン設計.md §2)。
		 * G-Buffer / フォワード描画の前の compute フェーズ。単一ビュー前提
		 * (分割画面では GpuClusterCuller::Get() を呼ばずスキップする)。
		 * 各アイテムの useGpuCull を確定させるため RenderFrame を non-const で受ける。
		 */
		class ClusterCullPass final : public IRenderPass
		{
		public:
			ClusterCullPass() = default;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "ClusterCullPass"; }

			bool IsSupported() const override;
			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;
		};
	}
}
