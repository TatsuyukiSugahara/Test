#include "aq.h"
#include "LightingDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include "Graphics/LightManager.h"
#include "Graphics/Lighting.h"

namespace aq
{
	namespace rendering
	{
		void LightingDebugPanel::RenderContent()
		{
			auto& lm = graphics::LightManager::Get();

			// ---- Ambient -------------------------------------------------------
			if (ImGui::CollapsingHeader("Ambient Light", ImGuiTreeNodeFlags_DefaultOpen))
			{
				auto& a = lm.Ambient();
				float col[3] = { a.color.x, a.color.y, a.color.z };
				if (ImGui::ColorEdit3("Color##ambient", col))
					a.color = { col[0], col[1], col[2] };
				ImGui::SliderFloat("Intensity##ambient", &a.intensity, 0.0f, 5.0f, "%.2f");
			}

			ImGui::Spacing();

			// ---- Directional Lights --------------------------------------------
			if (ImGui::CollapsingHeader("Directional Lights", ImGuiTreeNodeFlags_DefaultOpen))
			{
				int count = static_cast<int>(lm.GetDirectionalLightCount());
				if (ImGui::SliderInt("Count##dircount", &count, 1, static_cast<int>(graphics::MaxDirectionalLights)))
					lm.SetDirectionalLightCount(static_cast<uint32_t>(count));

				ImGui::Separator();

				for (uint32_t i = 0; i < lm.GetDirectionalLightCount(); ++i)
				{
					ImGui::PushID(static_cast<int>(i));

					char label[32];
					snprintf(label, sizeof(label), "Light %u", i);
					bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen);

					if (i == 0)
					{
						ImGui::SameLine();
						ImGui::TextDisabled("(Shadow)");
					}

					if (open)
					{
						auto& dl = lm.DirectionalAt(i);

						float col[3] = { dl.color.x, dl.color.y, dl.color.z };
						if (ImGui::ColorEdit3("Color", col))
							dl.color = { col[0], col[1], col[2] };

						ImGui::SliderFloat("Intensity", &dl.intensity, 0.0f, 10.0f, "%.2f");

						float dir[3] = { dl.direction.x, dl.direction.y, dl.direction.z };
						if (ImGui::DragFloat3("Direction", dir, 0.01f))
							dl.direction = { dir[0], dir[1], dir[2] };

						ImGui::TreePop();
					}

					ImGui::PopID();
				}
			}

			ImGui::Spacing();

			// ---- Specular ------------------------------------------------------
			if (ImGui::CollapsingHeader("Specular", ImGuiTreeNodeFlags_DefaultOpen))
			{
				float scale = lm.GetGlobalSpecularScale();
				if (ImGui::SliderFloat("Global Scale##spec", &scale, 0.0f, 5.0f, "%.2f"))
					lm.SetGlobalSpecularScale(scale);
			}
		}
	}
}
#endif
