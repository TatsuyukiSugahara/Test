#include "aq.h"
#include "PostProcessDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/PostProcess/Effects/MotionBlurEffect.h"
#include "Rendering/PostProcess/Effects/BloomEffect.h"
#include "Rendering/PostProcess/Effects/TonemapEffect.h"
#include "Graphics/IRenderTarget.h"
#include <imgui/imgui.h>

namespace aq
{
	namespace rendering
	{
		PostProcessDebugPanel::PostProcessDebugPanel(MotionBlurEffect* motionBlur, BloomEffect* bloom, TonemapEffect* tonemap)
			: motionBlur_(motionBlur)
			, bloom_(bloom)
			, tonemap_(tonemap)
		{}


		void PostProcessDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Post Effects", nullptr, &show_);
		}


		void PostProcessDebugPanel::RenderContent()
		{
			// --- モーションブラー ---
			if (motionBlur_)
			{
				ImGui::TextUnformatted("Motion Blur");
				float strength = motionBlur_->GetStrength();
				if (ImGui::SliderFloat("Strength", &strength, 0.0f, 2.0f, "%.2f"))
					motionBlur_->SetStrength(strength);
				ImGui::Separator();
			}

			// --- ブルーム (HDR) ---
			if (bloom_)
			{
				float threshold = bloom_->GetThreshold();
				float intensity = bloom_->GetIntensity();
				int   passes    = static_cast<int>(bloom_->GetBlurPasses());

				ImGui::TextUnformatted("Bloom (HDR)");
				if (ImGui::SliderFloat("Threshold", &threshold, 0.0f, 4.0f, "%.2f"))
					bloom_->SetThreshold(threshold);
				if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 4.0f, "%.2f"))
					bloom_->SetIntensity(intensity);
				if (ImGui::SliderInt("Blur Passes", &passes, 1, 4))
					bloom_->SetBlurPasses(static_cast<uint32_t>(passes));

				// 輝度抽出テクスチャのプレビュー
				ImGui::Separator();
				auto* brightRT = graphics::GraphicsDevice::Get().GetRenderTarget(bloom_->GetBrightRTHandle());
				if (brightRT)
				{
					const float w = ImGui::GetContentRegionAvail().x;
					ImGui::Image((ImTextureID)brightRT->GetRenderTargetSRV().GetNativeHandle(),
					             ImVec2(w, w * 0.5625f));  // 16:9 比率
					ImGui::Text("Bright Extract (threshold: %.2f)", threshold);
				}
			}

			// --- トーンマップ (HDR → LDR) ---
			if (tonemap_)
			{
				if (bloom_) { ImGui::Separator(); }
				ImGui::TextUnformatted("Tonemap");

				static const char* kModeNames[] = { "None (Clamp)", "Reinhard", "Reinhard Extended", "ACES", "Uncharted2" };
				int mode = static_cast<int>(tonemap_->GetTonemapMode());
				if (ImGui::Combo("Operator", &mode, kModeNames, IM_ARRAYSIZE(kModeNames)))
					tonemap_->SetTonemapMode(static_cast<TonemapEffect::TonemapMode>(mode));

				float exposure = tonemap_->GetExposure();
				if (ImGui::SliderFloat("Exposure", &exposure, 0.0f, 8.0f, "%.2f"))
					tonemap_->SetExposure(exposure);

				// ホワイトポイントは Reinhard Extended のときのみ有効
				if (tonemap_->GetTonemapMode() == TonemapEffect::TonemapMode::ReinhardExt)
				{
					float white = tonemap_->GetWhitePoint();
					if (ImGui::SliderFloat("White Point", &white, 1.0f, 16.0f, "%.2f"))
						tonemap_->SetWhitePoint(white);
				}

				bool applyGamma = tonemap_->GetApplyGamma();
				if (ImGui::Checkbox("Apply Gamma (sRGB encode)", &applyGamma))
					tonemap_->SetApplyGamma(applyGamma);
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("For a linear pipeline. Currently gamma space, so normally off.");
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
