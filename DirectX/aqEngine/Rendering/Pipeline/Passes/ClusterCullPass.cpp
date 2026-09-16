#include "aq.h"
#include "ClusterCullPass.h"
#include "Rendering/Occlusion/ClusterCull.h"
#include "Rendering/Occlusion/GpuClusterCuller.h"


namespace aq
{
	namespace rendering
	{
		bool ClusterCullPass::IsSupported() const
		{
			return graphics::IsComputeSupported();
		}


		void ClusterCullPass::DeclareResources(PassDeclaration& decl) const
		{
			// アイテムの useGpuCull を確定させるだけで PassResources は読み書きしない。
			(void)decl;
		}


		void ClusterCullPass::Build(RenderFrame& frame, const PassViewInfo& view,
		                            PassResources& res, RenderCommandList& outList)
		{
			(void)res;

			// GPU クラスタカリングは単一カメラ前提(分割画面では使わない。今の Views 経路と同じ)。
			if (!view.IsSingleView()) { return; }
			if (!(IsClusterCullEnabled() && GpuClusterCuller::Get().IsReady())) { return; }

			// 小メッシュは dispatch/間接描画の固定コストが削減効果を上回るため閾値でスキップ。
			const uint32_t minClusters = GetClusterCullMinClusters();
			auto runCull = [&](std::vector<RenderItem>& items)
			{
				for (RenderItem& item : items)
				{
					if (item.clusterCount >= minClusters && item.gpuOutIndices && item.gpuArgs)
					{
						item.useGpuCull = true;
						outList.Enqueue<ClusterCullCommand>(item, frame.camera);
					}
				}
			};
			runCull(frame.items);
			runCull(frame.forwardItems);
		}
	}
}
