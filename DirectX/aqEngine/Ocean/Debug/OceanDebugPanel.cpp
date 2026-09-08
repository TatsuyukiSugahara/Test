#include "aq.h"
#include "OceanDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include "Component/OceanComponent.h"

namespace aq
{
	namespace ocean
	{
		void OceanDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Ocean", nullptr, &show_);
		}


		void OceanDebugPanel::RenderContent()
		{
			OceanParams* params = nullptr;
			ecs::Foreach<ecs::OceanComponent>(
				[&params](const ecs::Entity&, ecs::OceanComponent* comp)
				{
					if (!params && comp->IsCompleted())
						params = &comp->GetParams();
				});

			if (!params)
			{
				ImGui::TextDisabled("OceanComponent not found");
				return;
			}

			// --- Gerstner 波 ---
			if (ImGui::CollapsingHeader("Gerstner Waves", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::SliderFloat("Steepness (waveQ)", &params->waveQ, 0.0f, 1.0f, "%.2f");
				ImGui::TextDisabled("0 = smooth sine  1 = sharp crest");
				ImGui::Separator();
				static const char* kLabels[4] =
				{
					"Wave 0  (main swell)",
					"Wave 1  (diagonal)",
					"Wave 2  (reverse diagonal)",
					"Wave 3  (chop)",
				};
				for (int i = 0; i < 4; ++i)
				{
					ImGui::PushID(i);
					if (ImGui::TreeNode(kLabels[i]))
					{
						ImGui::SliderFloat2("Direction (dirX, dirZ)", &params->waves[i].dirX,    -1.0f, 1.0f,   "%.2f");
						ImGui::SliderFloat("Amplitude (m)",           &params->waves[i].amplitude, 0.0f, 10.0f,  "%.2f");
						ImGui::SliderFloat("Wavelength (m)",           &params->waves[i].wavelength,1.0f, 200.0f, "%.1f");
						ImGui::SliderFloat("Speed (m/s)",         &params->waves[i].speed,     0.0f, 20.0f,  "%.2f");
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
			}

			// --- Fresnel ---
			if (ImGui::CollapsingHeader("Fresnel"))
			{
				ImGui::SliderFloat("Bias",      &params->fresnelBias,  0.0f, 1.0f,  "%.3f");
				ImGui::SliderFloat("Scale",     &params->fresnelScale, 0.0f, 2.0f,  "%.3f");
				ImGui::SliderFloat("Power",     &params->fresnelPower, 1.0f, 10.0f, "%.1f");
			}

			// --- 太陽ハイライト ---
			if (ImGui::CollapsingHeader("Sun Highlight"))
			{
				ImGui::SliderFloat("Shininess"          , &params->sunShininess, 8.0f,  2048.0f, "%.0f");
				ImGui::SliderFloat("Intensity",   &params->sunIntensity, 0.0f,  10.0f,   "%.2f");
				ImGui::ColorEdit3("Sky reflection color",            &params->skyColor.x);
			}

			// --- 海の色 ---
			if (ImGui::CollapsingHeader("Water Color"))
			{
				ImGui::ColorEdit3("Deep color",   &params->deepColor.x);
				ImGui::ColorEdit3("Shallow color"   , &params->shallowColor.x);
			}

			// --- UV スクロール法線マップ ---
			if (ImGui::CollapsingHeader("UV Scroll (normal maps)"))
			{
				ImGui::PushID("nm1");
				ImGui::Text("Normal map 1");
				ImGui::SliderFloat("Scale",         &params->normalScale1, 0.1f, 10.0f, "%.2f");
				ImGui::SliderFloat2("Direction (XZ)", &params->normalDirX1,  -1.0f, 1.0f, "%.2f");
				ImGui::SliderFloat("Speed",         &params->normalSpeed1,  0.0f, 0.5f, "%.4f");
				ImGui::PopID();

				ImGui::Separator();

				ImGui::PushID("nm2");
				ImGui::Text("Normal map 2");
				ImGui::SliderFloat("Scale",         &params->normalScale2, 0.1f, 10.0f, "%.2f");
				ImGui::SliderFloat2("Direction (XZ)", &params->normalDirX2,  -1.0f, 1.0f, "%.2f");
				ImGui::SliderFloat("Speed",         &params->normalSpeed2,  0.0f, 0.5f, "%.4f");
				ImGui::PopID();
			}
		}


		void OceanDebugPanel::DebugRender()
		{
			if (!show_) return;
			if (ImGui::Begin("Ocean"))
				RenderContent();
			ImGui::End();
		}
	}
}
#endif
