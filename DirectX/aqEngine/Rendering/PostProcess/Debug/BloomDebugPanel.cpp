#include "aq.h"
#include "BloomDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
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
			ImGui::MenuItem("ポストエフェクト", nullptr, &show_);
		}


		void BloomDebugPanel::RenderContent()
		{
			float threshold = renderer_.GetThreshold();
			float intensity = renderer_.GetIntensity();
			int   passes    = static_cast<int>(renderer_.GetBlurPasses());

			ImGui::TextUnformatted("Bloom (HDR)");
			if (ImGui::SliderFloat("Threshold", &threshold, 0.0f, 4.0f, "%.2f"))
				renderer_.SetThreshold(threshold);
			if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 4.0f, "%.2f"))
				renderer_.SetIntensity(intensity);
			if (ImGui::SliderInt("Blur Passes", &passes, 1, 4))
				renderer_.SetBlurPasses(static_cast<uint32_t>(passes));

			// --- トーンマップ (HDR → LDR) ---
			ImGui::Separator();
			ImGui::TextUnformatted("Tonemap");

			static const char* kModeNames[] = { "None (Clamp)", "Reinhard", "Reinhard Extended", "ACES", "Uncharted2" };
			int mode = static_cast<int>(renderer_.GetTonemapMode());
			if (ImGui::Combo("Operator", &mode, kModeNames, IM_ARRAYSIZE(kModeNames)))
				renderer_.SetTonemapMode(static_cast<BloomRenderer::TonemapMode>(mode));

			float exposure = renderer_.GetExposure();
			if (ImGui::SliderFloat("Exposure", &exposure, 0.0f, 8.0f, "%.2f"))
				renderer_.SetExposure(exposure);

			// ホワイトポイントは Reinhard Extended のときのみ有効
			if (renderer_.GetTonemapMode() == BloomRenderer::TonemapMode::ReinhardExt)
			{
				float white = renderer_.GetWhitePoint();
				if (ImGui::SliderFloat("White Point", &white, 1.0f, 16.0f, "%.2f"))
					renderer_.SetWhitePoint(white);
			}

			bool applyGamma = renderer_.GetApplyGamma();
			if (ImGui::Checkbox("Apply Gamma (sRGB encode)", &applyGamma))
				renderer_.SetApplyGamma(applyGamma);
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("リニアパイプライン用。現状ガンマ空間のため通常 off。");

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
			if (ImGui::Begin("ポストエフェクト"))
				RenderContent();
			ImGui::End();
		}
	}
}
#endif
