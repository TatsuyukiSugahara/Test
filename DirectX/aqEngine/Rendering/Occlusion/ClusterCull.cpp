#include "aq.h"
#include "ClusterCull.h"
#include "Rendering/RenderFrame.h"
#include "Graphics/RenderContext.h"
#include "Graphics/Meshlet.h"
#include "Math/Bounds.h"
#include <atomic>
#include <vector>


namespace aq
{
	namespace rendering
	{
		namespace
		{
			// 既定OFF: CPU方式 (毎フレーム compact + IB アップロード) は小メッシュで割に合わない。
			// 巨大メッシュ / GPU 律速シーンで手動 ON するための機能。
			std::atomic<bool>     g_clusterCullEnabled{ false };
			// 小メッシュは dispatch/間接描画の固定コストが効果を上回るため、これ未満は通常描画。
			std::atomic<uint32_t> g_clusterCullMinClusters{ 256u };
		}


		void SetClusterCullEnabled(bool enabled) { g_clusterCullEnabled.store(enabled); }
		bool IsClusterCullEnabled()              { return g_clusterCullEnabled.load(); }

		void     SetClusterCullMinClusters(uint32_t m) { g_clusterCullMinClusters.store(m); }
		uint32_t GetClusterCullMinClusters()           { return g_clusterCullMinClusters.load(); }


		uint32_t BindCulledIndices(graphics::RenderContext& ctx,
		                           const RenderItem& item, const CameraData& camera)
		{
			// CPU 方式は次の経路でのみ走る (DX12 主経路は GPU 駆動の DrawIndexedIndirect):
			//   - DX11 (GPU 駆動クラスタ未対応 → gpuOutIndices/gpuArgs が無く useGpuCull=false)
			//   - 閾値未満で GPU 機構をスキップしたメッシュ (下の min-clusters ゲートで通常描画に倒す)
			// min-clusters ゲートを GPU 経路 (Renderer.cpp) と揃え、小メッシュを CPU でも cull しない。
			if (g_clusterCullEnabled.load() &&
			    item.cullIndexBuffer && item.clusters && item.reorderedIndices &&
			    !item.clusters->empty() &&
			    item.clusters->size() >= g_clusterCullMinClusters.load())
			{
				math::Matrix4x4 viewProj;
				viewProj.Mull(camera.viewMatrix, camera.projectionMatrix);
				math::Frustum frustum;
				frustum.FromViewProjection(viewProj);

				const std::vector<graphics::MeshCluster>& clusters = *item.clusters;
				const std::vector<uint32_t>&              src      = *item.reorderedIndices;

				// 可視クラスタの範囲を連結 (スレッドローカルで再利用しアロケーションを抑える)
				thread_local std::vector<uint32_t> compacted;
				compacted.clear();
				for (const graphics::MeshCluster& cl : clusters)
				{
					if (!graphics::IsClusterVisible(cl, item.worldMatrix, frustum, camera.position))
						continue;
					const uint32_t b = cl.triOffset * 3u;
					const uint32_t e = b + cl.triCount * 3u;
					if (e <= src.size())
						compacted.insert(compacted.end(), src.begin() + b, src.begin() + e);
				}

				if (compacted.empty())
				{
					// 全クラスタ不可視: 何も描かない (IB は通常のものをバインドしておく)
					ctx.IASetIndexBuffer(*item.indexBuffer);
					return 0;
				}
				if (compacted.size() < src.size())
				{
					item.cullIndexBuffer->Update(
						compacted.data(), static_cast<uint32_t>(compacted.size() * sizeof(uint32_t)));
					ctx.IASetIndexBuffer(*item.cullIndexBuffer);
					return static_cast<uint32_t>(compacted.size());
				}
				// 何も削減されなかった → 通常描画にフォールスルー
			}

			ctx.IASetIndexBuffer(*item.indexBuffer);
			return item.indexCount;
		}
	}
}
