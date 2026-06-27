#include "aq.h"
#include "CullingDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include "Component/BodyComponentSystem.h"
#include "Rendering/Occlusion/ClusterCull.h"


namespace aq
{
	namespace rendering
	{
		void CullingDebugPanel::RenderContent()
		{
			if (!ecs::RenderSystem::IsAvailable())
			{
				ImGui::TextDisabled("(RenderSystem 未初期化)");
				return;
			}

			// フラスタムカリング 可視/総数カウンタ (メインカメラ)
			const uint32_t total   = ecs::RenderSystem::GetCullingTotalCount();
			const uint32_t visible = ecs::RenderSystem::GetCullingVisibleCount();
			const uint32_t culled  = (total >= visible) ? (total - visible) : 0u;

			bool cullFrustum = ecs::RenderSystem::IsFrustumCullingEnabled();
			if (ImGui::Checkbox("Frustum Culling", &cullFrustum))
				ecs::RenderSystem::SetFrustumCullingEnabled(cullFrustum);
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
				"visible %u / %u  (culled %u)", visible, total, culled);

			// オクリュージョン (Hi-Z)
			if (ecs::RenderSystem::IsOcclusionAvailable())
			{
				bool occ = ecs::RenderSystem::IsOcclusionCullingEnabled();
				if (ImGui::Checkbox("Occlusion Culling", &occ))
					ecs::RenderSystem::SetOcclusionCullingEnabled(occ);
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.7f, 1.0f),
					"occluded %u", ecs::RenderSystem::GetOccludedCount());
			}

			// トライアングル(クラスタ)カリング — GPU 駆動 (実描画) と CPU 統計を分離
			{
				// GPU カリング本体 (レンダースレッドで compute → ExecuteIndirect)
				bool cull = rendering::IsClusterCullEnabled();
				if (ImGui::Checkbox("Cluster Culling (GPU)", &cull))
					rendering::SetClusterCullEnabled(cull);

				// 適用する最小クラスタ数 (小メッシュは固定コスト>効果のためスキップ)
				ImGui::SameLine();
				int minCl = static_cast<int>(rendering::GetClusterCullMinClusters());
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::DragInt("min clusters", &minCl, 4.0f, 0, 4096))
					rendering::SetClusterCullMinClusters(static_cast<uint32_t>(minCl < 0 ? 0 : minCl));

				// 統計表示 (ゲームスレッドで毎クラスタ判定するため CPU コストあり・既定OFF)
				ImGui::SameLine();
				bool stats = ecs::RenderSystem::IsClusterStatsEnabled();
				if (ImGui::Checkbox("Stats", &stats))
					ecs::RenderSystem::SetClusterStatsEnabled(stats);

				if (stats)
				{
					const uint32_t clTot  = ecs::RenderSystem::GetClusterTotal();
					const uint32_t clVis  = ecs::RenderSystem::GetClusterVisible();
					const uint32_t triTot = ecs::RenderSystem::GetClusterTriTotal();
					const uint32_t triVis = ecs::RenderSystem::GetClusterTriVisible();
					const float triPct = (triTot > 0) ? (100.0f * triVis / triTot) : 0.0f;
					ImGui::TextColored(ImVec4(0.85f, 0.8f, 1.0f, 1.0f),
						"clusters %u/%u  tris %u/%u (%.0f%% drawn, %u culled)",
						clVis, clTot, triVis, triTot, triPct, (triTot >= triVis) ? (triTot - triVis) : 0u);
					ImGui::TextDisabled("注: Stats は CPU 側 (frustum+backface のみ・Hi-Z 非対象)。GPU 実描画数とは別。");
				}
				else
				{
					ImGui::TextDisabled("(Stats OFF: %% drawn を見るなら Stats を ON。CPU コスト増)");
				}
			}
		}
	}
}
#endif
