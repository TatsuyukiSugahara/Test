#include "aq.h"
#include "SSAODebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include "SSAORenderer.h"
#include "Graphics/IRenderTarget.h"
#include <imgui/imgui.h>


namespace aq
{
	namespace rendering
	{
		SSAODebugPanel::SSAODebugPanel(SSAORenderer& ssao)
			: ssao_(ssao)
		{
		}


		void SSAODebugPanel::RenderContent()
		{
			bool enabled = ssao_.IsEnabled();
			if (ImGui::Checkbox("SSAO Enabled", &enabled)) {
				ssao_.SetEnabled(enabled);
			}
			ImGui::Text("State: %s", ssao_.IsActive() ? "Active" : "Inactive");

			bool debugView = ssao_.IsDebugView();
			if (ImGui::Checkbox("Show AO", &debugView)) {
				ssao_.SetDebugView(debugView);
			}
			ImGui::Separator();

			float radius = ssao_.GetRadius();
			if (ImGui::SliderFloat("Radius", &radius, 0.1f, 4.0f, "%.2f")) {
				ssao_.SetRadius(radius);
			}

			float intensity = ssao_.GetIntensity();
			if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 4.0f, "%.2f")) {
				ssao_.SetIntensity(intensity);
			}

			float power = ssao_.GetPower();
			if (ImGui::SliderFloat("Power", &power, 0.25f, 4.0f, "%.2f")) {
				ssao_.SetPower(power);
			}

			float biasScale = ssao_.GetBiasScale();
			if (ImGui::SliderFloat("Bias Scale", &biasScale, 0.0f, 0.02f, "%.4f")) {
				ssao_.SetBiasScale(biasScale);
			}

			int sampleCount = static_cast<int>(ssao_.GetSampleCount());
			if (ImGui::SliderInt("Sample Count", &sampleCount, 4, 32)) {
				ssao_.SetSampleCount(static_cast<uint32_t>(sampleCount));
			}

			float maxScreenRadius = ssao_.GetMaxScreenRadius();
			if (ImGui::SliderFloat("Max Screen Radius", &maxScreenRadius, 8.0f, 128.0f, "%.0f px")) {
				ssao_.SetMaxScreenRadius(maxScreenRadius);
			}

			float planeTolerance = ssao_.GetPlaneTolerance();
			if (ImGui::SliderFloat("Blur Plane Tolerance", &planeTolerance, 0.005f, 0.2f, "%.3f")) {
				ssao_.SetPlaneTolerance(planeTolerance);
			}
			ImGui::Separator();

			// ぼかした AO の RT (R32_Float。r だけが入っているので赤の濃淡で見える)
			ImGui::SliderFloat("Preview Width", &previewWidth_, 160.0f, 640.0f, "%.0f");
			auto* ao = graphics::GraphicsDevice::Get().GetRenderTarget(ssao_.GetBlurredAOHandle());
			if (ao)
			{
				// RT は描画解像度と同じ寸法 (IRenderTarget は寸法を返さないので Engine から取る)
				const int32_t renderW = Engine::Get().GetRenderWidth();
				const int32_t renderH = Engine::Get().GetRenderHeight();
				const float   aspect  = (renderW > 0)
					? static_cast<float>(renderH) / static_cast<float>(renderW)
					: 0.5625f;
				ImGui::TextUnformatted("Blurred AO (R channel)");
				ImGui::Image((ImTextureID)ao->GetRenderTargetSRV().GetNativeHandle(),
				             ImVec2(previewWidth_, previewWidth_ * aspect));
			}
			else
			{
				ImGui::TextUnformatted("Blurred AO: not created");
			}
		}
	}
}
#endif
