#include "aq.h"
#include "EntityContext.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#endif


namespace aq
{
	namespace ecs
	{
		EntityContext* EntityContext::instance_ = nullptr;


#ifdef AQ_DEBUG_IMGUI
		void EntityContext::DebugRenderMenu()
		{
			if (ImGui::BeginMenu("ECS"))
			{
				ImGui::MenuItem("System Graph", nullptr, &showSystemGraph_);
				ImGui::EndMenu();
			}
		}

		void EntityContext::DebugRender()
		{
			// 各 System の個別デバッグウィンドウ
			systemManager_.DebugRenderAll();

			if (!showSystemGraph_) return;

			// -------- ECS System 依存グラフ --------
			const size_t n = systemManager_.GetSystemCount();
			if (n == 0) return;

			ImGui::SetNextWindowSize({ 900.0f, 500.0f }, ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("ECS System Graph"))
			{
				ImGui::End();
				return;
			}

			ImGui::TextDisabled("Green: no deps  /  Blue: has deps  /  Arrow: dependency direction");
			ImGui::Separator();

			// --- トポロジカル深度 ---
			std::vector<int> depth(n, 0);
			for (int pass = 0; pass < static_cast<int>(n); ++pass)
				for (size_t i = 0; i < n; ++i)
					for (size_t d : systemManager_.GetSystemDependencies(i))
						if (depth[i] <= depth[d])
							depth[i] = depth[d] + 1;

			int maxDepth = *std::max_element(depth.begin(), depth.end());
			std::vector<std::vector<size_t>> levels(maxDepth + 1);
			for (size_t i = 0; i < n; ++i)
				levels[depth[i]].push_back(i);

			// --- ノード幅を名前の最大幅に合わせる ---
			float maxNameW = 0.0f;
			for (size_t i = 0; i < n; ++i)
				maxNameW = std::max(maxNameW, ImGui::CalcTextSize(systemManager_.GetSystemDisplayName(i).c_str()).x);

			const float nodeW        = maxNameW + 24.0f;
			const float nodeH        = 52.0f;
			const float gapX         = 80.0f;
			const float gapY         = 28.0f;
			const float padding      = 24.0f;
			const float cornerRadius = 8.0f;

			// --- ノード座標（キャンバス原点相対） ---
			std::vector<ImVec2> pos(n);
			size_t maxRows = 0;
			for (auto& lv : levels) maxRows = std::max(maxRows, lv.size());

			for (int lvl = 0; lvl <= maxDepth; ++lvl)
			{
				float x      = padding + lvl * (nodeW + gapX);
				float colH   = static_cast<float>(levels[lvl].size()) * (nodeH + gapY) - gapY;
				float totalH = static_cast<float>(maxRows)            * (nodeH + gapY) - gapY;
				float y      = padding + (totalH - colH) * 0.5f;
				for (size_t idx : levels[lvl])
				{
					pos[idx] = { x, y };
					y += nodeH + gapY;
				}
			}

			float canvasW = padding * 2.0f + (maxDepth + 1) * (nodeW + gapX) - gapX;
			float canvasH = padding * 2.0f + static_cast<float>(maxRows) * (nodeH + gapY) - gapY;

			// --- スクロール可能なキャンバス ---
			ImGui::BeginChild("##ecsgraph", { 0.0f, 0.0f }, false,
				ImGuiWindowFlags_HorizontalScrollbar);

			ImVec2 orig = ImGui::GetCursorScreenPos();
			ImGui::Dummy({ canvasW, canvasH });
			ImDrawList* dl = ImGui::GetWindowDrawList();

			// 背景
			dl->AddRectFilled(orig, { orig.x + canvasW, orig.y + canvasH },
				IM_COL32(28, 30, 38, 255));

			// --- エッジ（ベジェ曲線 + 矢印） ---
			for (size_t i = 0; i < n; ++i)
			{
				for (size_t d : systemManager_.GetSystemDependencies(i))
				{
					ImVec2 p1{ orig.x + pos[d].x + nodeW,
					           orig.y + pos[d].y + nodeH * 0.5f };
					ImVec2 p2{ orig.x + pos[i].x,
					           orig.y + pos[i].y + nodeH * 0.5f };
					float cx = (p1.x + p2.x) * 0.5f;
					dl->AddBezierCubic(p1, { cx, p1.y }, { cx, p2.y }, p2,
						IM_COL32(255, 200, 60, 200), 2.5f);
					dl->AddTriangleFilled(
						p2,
						{ p2.x - 9.0f, p2.y - 5.5f },
						{ p2.x - 9.0f, p2.y + 5.5f },
						IM_COL32(255, 200, 60, 255));
				}
			}

			// --- ノード ---
			for (size_t i = 0; i < n; ++i)
			{
				ImVec2 tl{ orig.x + pos[i].x, orig.y + pos[i].y };
				ImVec2 br{ tl.x + nodeW,       tl.y + nodeH };

				bool  isLeaf = systemManager_.GetSystemDependencies(i).empty();
				ImU32 fill   = isLeaf ? IM_COL32(40, 80,  55, 235) : IM_COL32(45, 60, 120, 235);
				ImU32 border = isLeaf ? IM_COL32(70, 200, 100, 255) : IM_COL32(100, 150, 255, 255);

				dl->AddRectFilled(tl, br, fill,          cornerRadius);
				dl->AddRect      (tl, br, border, cornerRadius, 0, 2.0f);

				const std::string& name = systemManager_.GetSystemDisplayName(i);
				ImVec2 tsz = ImGui::CalcTextSize(name.c_str());
				ImVec2 tp{
					tl.x + (nodeW - tsz.x) * 0.5f,
					tl.y + (nodeH - tsz.y) * 0.5f
				};
				dl->AddText(tp, IM_COL32(220, 235, 255, 255), name.c_str());
			}

			ImGui::EndChild();
			ImGui::End();
		}
#endif

	}
}
