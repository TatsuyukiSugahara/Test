#include "aq.h"
#include "SSRDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include "SSRRenderer.h"
#include <imgui/imgui.h>


namespace aq
{
	namespace rendering
	{
		SSRDebugPanel::SSRDebugPanel(SSRRenderer& ssr)
			: ssr_(ssr)
		{
		}


		void SSRDebugPanel::RenderContent()
		{
			bool enabled = ssr_.IsEnabled();
			if (ImGui::Checkbox("SSR Enabled", &enabled)) {
				ssr_.SetEnabled(enabled);
			}
			ImGui::Text("State: %s", ssr_.IsActive() ? "Active" : "Inactive");

			bool debugView = ssr_.IsDebugView();
			if (ImGui::Checkbox("Show Confidence", &debugView)) {
				ssr_.SetDebugView(debugView);
			}
			ImGui::Separator();

			float maxDistance = ssr_.GetMaxDistance();
			if (ImGui::SliderFloat("Max Distance", &maxDistance, 5.0f, 200.0f, "%.1f")) {
				ssr_.SetMaxDistance(maxDistance);
			}

			float thicknessScale = ssr_.GetThicknessScale();
			if (ImGui::SliderFloat("Thickness Scale", &thicknessScale, 0.001f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
				ssr_.SetThicknessScale(thicknessScale);
			}

			int maxSteps = static_cast<int>(ssr_.GetMaxSteps());
			if (ImGui::SliderInt("Max Steps", &maxSteps, 8, 128)) {
				ssr_.SetMaxSteps(static_cast<uint32_t>(maxSteps));
			}
			ImGui::Separator();

			// 開始 <= 終了を保つ (動かした側に、もう一方を寄せる)
			float fadeStart = ssr_.GetRoughnessFadeStart();
			float fadeEnd   = ssr_.GetRoughnessFadeEnd();
			if (ImGui::SliderFloat("Roughness Fade Start", &fadeStart, 0.0f, 1.0f, "%.2f")) {
				if (fadeEnd < fadeStart) fadeEnd = fadeStart;
				ssr_.SetRoughnessFade(fadeStart, fadeEnd);
			}
			if (ImGui::SliderFloat("Roughness Fade End", &fadeEnd, 0.0f, 1.0f, "%.2f")) {
				if (fadeStart > fadeEnd) fadeStart = fadeEnd;
				ssr_.SetRoughnessFade(fadeStart, fadeEnd);
			}
		}
	}
}
#endif
