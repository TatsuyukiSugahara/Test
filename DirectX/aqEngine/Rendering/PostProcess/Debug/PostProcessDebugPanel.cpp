#include "aq.h"
#include "PostProcessDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/PostProcess/PostProcessChain.h"
#include "Graphics/IRenderTarget.h"
#include <imgui/imgui.h>

namespace aq
{
	namespace rendering
	{
		PostProcessDebugPanel::PostProcessDebugPanel(PostProcessChain& chain)
			: chain_(chain)
		{}


		void PostProcessDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Post Effects", nullptr, &show_);
		}


		void PostProcessDebugPanel::RenderContent()
		{
			BloomPass&   bloom   = chain_.GetBloomPass();
			TonemapPass& tonemap = chain_.GetTonemapPass();

			float threshold = bloom.GetThreshold();
			float intensity = bloom.GetIntensity();
			int   passes    = static_cast<int>(bloom.GetBlurPasses());

			ImGui::TextUnformatted("Bloom (HDR)");
			if (ImGui::SliderFloat("Threshold", &threshold, 0.0f, 4.0f, "%.2f"))
				bloom.SetThreshold(threshold);
			if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 4.0f, "%.2f"))
				bloom.SetIntensity(intensity);
			if (ImGui::SliderInt("Blur Passes", &passes, 1, 4))
				bloom.SetBlurPasses(static_cast<uint32_t>(passes));

			// --- トーンマップ (HDR → LDR) ---
			ImGui::Separator();
			ImGui::TextUnformatted("Tonemap");

			static const char* kModeNames[] = { "None (Clamp)", "Reinhard", "Reinhard Extended", "ACES", "Uncharted2" };
			int mode = static_cast<int>(tonemap.GetTonemapMode());
			if (ImGui::Combo("Operator", &mode, kModeNames, IM_ARRAYSIZE(kModeNames)))
				tonemap.SetTonemapMode(static_cast<TonemapPass::TonemapMode>(mode));

			float exposure = tonemap.GetExposure();
			if (ImGui::SliderFloat("Exposure", &exposure, 0.0f, 8.0f, "%.2f"))
				tonemap.SetExposure(exposure);

			// ホワイトポイントは Reinhard Extended のときのみ有効
			if (tonemap.GetTonemapMode() == TonemapPass::TonemapMode::ReinhardExt)
			{
				float white = tonemap.GetWhitePoint();
				if (ImGui::SliderFloat("White Point", &white, 1.0f, 16.0f, "%.2f"))
					tonemap.SetWhitePoint(white);
			}

			bool applyGamma = tonemap.GetApplyGamma();
			if (ImGui::Checkbox("Apply Gamma (sRGB encode)", &applyGamma))
				tonemap.SetApplyGamma(applyGamma);
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("For a linear pipeline. Currently gamma space, so normally off.");

			// 輝度抽出テクスチャのプレビュー
			ImGui::Separator();
			auto* brightRT = graphics::GraphicsDevice::Get().GetRenderTarget(bloom.GetBrightRTHandle());
			if (brightRT)
			{
				const float w = ImGui::GetContentRegionAvail().x;
				ImGui::Image((ImTextureID)brightRT->GetRenderTargetSRV().GetNativeHandle(),
				             ImVec2(w, w * 0.5625f));  // 16:9 比率
				ImGui::Text("Bright Extract (threshold: %.2f)", threshold);
			}
		}


		void PostProcessDebugPanel::DebugRender()
		{
			if (!show_) return;
			if (ImGui::Begin("Post Effects"))
				RenderContent();
			ImGui::End();
		}
	}
}
#endif
