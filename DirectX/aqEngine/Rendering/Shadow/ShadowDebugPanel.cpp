#include "aq.h"
#include "ShadowDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>

namespace aq
{
	namespace rendering
	{
		ShadowDebugPanel::ShadowDebugPanel(ShadowSettings& settings)
			: settings_(settings)
		{
		}


		void ShadowDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Shadow", nullptr, &show_);
		}


		void ShadowDebugPanel::DebugRender()
		{
			if (!show_) return;

			if (ImGui::Begin("Shadow"))
			{
				ImGui::SliderFloat("Depth Bias",   &settings_.depthBias,   0.0f,  0.05f,  "%.4f");
				ImGui::SliderFloat("PCF Softness", &settings_.softness,    0.0f,  8.0f,   "%.2f");
				ImGui::SliderFloat("Ortho Width",  &settings_.orthoWidth,  10.0f, 200.0f, "%.1f");
				ImGui::SliderFloat("Ortho Height", &settings_.orthoHeight, 10.0f, 200.0f, "%.1f");
				ImGui::SliderFloat("Far Plane",    &settings_.farPlane,    10.0f, 500.0f, "%.1f");
				ImGui::Separator();
				const auto& c = settings_.sceneCenter;
				ImGui::Text("Scene Center  (%.2f, %.2f, %.2f)", c.x, c.y, c.z);
			}
			ImGui::End();
		}
	}
}
#endif
