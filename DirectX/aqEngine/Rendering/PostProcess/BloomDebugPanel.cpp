#include "aq.h"
#include "BloomDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include "BloomRenderer.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/IRenderTarget.h"
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

			// 輝度抽出テクスチャのプレビュー
			ImGui::Separator();
			auto* brightRT = graphics::GraphicsDevice::Get().GetRenderTarget(renderer_.GetBrightRTHandle());
			if (brightRT)
			{
				const float w = ImGui::GetContentRegionAvail().x;
				ImGui::Image((ImTextureID)brightRT->GetRenderTargetSRV().GetNativeHandle(),
				             ImVec2(w, w * 0.5625f));  // 16:9 比率
				ImGui::Text("Bright Extract (threshold: %.2f)", threshold);
			}
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
