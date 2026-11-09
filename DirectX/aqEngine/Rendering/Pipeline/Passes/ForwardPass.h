#pragma once
#include <cstdint>
#include <vector>
#include "Rendering/Pipeline/IRenderPass.h"
#include "Rendering/RenderFrame.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * フォワードパス(設計書/レンダーパイプライン設計.md §2)。
		 * Renderer::RecordDrawItem + recordForwardItems + InstancedDrawItemCommand を移したもの。
		 * GBufferPass が無い構成(フォワードのみ)では frame.items(deferred 用アイテム)も
		 * ここでまとめて描く。
		 */
		class ForwardPass final : public IRenderPass
		{
		public:
			ForwardPass() = default;


			/**
			 * IRenderPass
			 */
		public:
			inline const char* GetName() const override { return "ForwardPass"; }

			inline ResizePolicy GetResizePolicy() const override { return ResizePolicy::Independent; }
			void DeclareResources(PassDeclaration& decl) const override;
			void Build(RenderFrame& frame, const PassViewInfo& view,
			           PassResources& res, RenderCommandList& outList) override;


			/**
			 * 半透明アイテムを奥→手前の順に並べた添字を返す(設計書/エンジン課題解消設計.md P3-B 案 A)。
			 *
			 * 対象は items のうち `translucent == true` のものだけで、outOrder には items への添字を入れる。
			 * キーは**ビュー空間 Z**(ユークリッド距離ではない。画面に垂直な奥行きが合成順に効くため)。
			 * 位置は `hasBounds` なら localBounds の中心、そうでなければローカル原点を worldMatrix で変換した点。
			 * 左手系なので手前ほど Z が小さく、Z の大きい(奥の)ものから並べる。
			 * **同じ Z は収集順を保つ**(stable_sort。毎フレーム同じ入力は同じ順序)。非有限の Z は 0 として扱う。
			 * テストできるよう public static にしてある。
			 */
			static void SortTranslucentBackToFront(const std::vector<RenderItem>& items,
			                                       const math::Matrix4x4& viewMatrix,
			                                       std::vector<uint32_t>& outOrder);
		};
	}
}
