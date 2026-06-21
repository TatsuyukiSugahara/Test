#include "aq.h"
#include "BloomDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include "BloomRenderer.h"
#include <imgui/imgui.h>

namespace aq
{
	namespace rendering
	{
		BloomDebugPanel::BloomDebugPanel(BloomRenderer& renderer)
			: renderer_(renderer)
		{}


		void BloomDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Bloom", nullptr, &show_);
		}


		void BloomDebugPanel::RenderContent()
		{
			float threshold = renderer_.GetThreshold();
			float intensity = renderer_.GetIntensity();
			int   passes    = static_cast<int>(renderer_.GetBlurPasses());

			if (ImGui::SliderFloat("Threshold", &threshold, 0.0f, 1.0f, "%.2f"))
				renderer_.SetThreshold(threshold);
			if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 4.0f, "%.2f"))
				renderer_.SetIntensity(intensity);
			if (ImGui::SliderInt("Blur Passes", &passes, 1, 4))
				renderer_.SetBlurPasses(static_cast<uint32_t>(passes));
		}


		void BloomDebugPanel::DebugRender()
		{
			if (!show_) return;
			if (ImGui::Begin("Bloom"))
				RenderContent();
			ImGui::End();
		}
	}
}
#endif
