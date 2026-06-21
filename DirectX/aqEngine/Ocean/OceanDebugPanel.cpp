#include "aq.h"
#include "OceanDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include "Component/OceanComponent.h"
#include "ECS/ECS.h"

namespace aq
{
	namespace ocean
	{
		void OceanDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Ocean", nullptr, &show_);
		}


		void OceanDebugPanel::DebugRender()
		{
			if (!show_) return;

			// ECS から最初の OceanComponent を毎フレーム検索する
			OceanParams* params = nullptr;
			ecs::Foreach<ecs::OceanComponent>(
				[&params](const ecs::Entity&, ecs::OceanComponent* comp)
				{
					if (!params && comp->IsCompleted())
						params = &comp->GetParams();
				});

			if (ImGui::Begin("Ocean"))
			{
				if (!params)
				{
					ImGui::TextDisabled("OceanComponent が見つかりません");
					ImGui::End();
					return;
				}

				// --- Gerstner 波 ---
				if (ImGui::CollapsingHeader("Gerstner 波", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::SliderFloat("水平変位 waveQ", &params->waveQ, 0.0f, 1.0f, "%.2f");
					ImGui::TextDisabled("0=滑らかな正弦波  1=急峻な峰");
					ImGui::Separator();
					static const char* kLabels[4] =
					{
						"Wave 0  (主うねり)",
						"Wave 1  (斜め)",
						"Wave 2  (逆斜め)",
						"Wave 3  (チョップ)",
					};
					for (int i = 0; i < 4; ++i)
					{
						ImGui::PushID(i);
						if (ImGui::TreeNode(kLabels[i]))
						{
							// dirX・dirZ は struct 内で隣接しているため SliderFloat2 で一括編集
							ImGui::SliderFloat2("方向 (dirX, dirZ)", &params->waves[i].dirX,    -1.0f, 1.0f,   "%.2f");
							ImGui::SliderFloat("振幅 (m)",           &params->waves[i].amplitude, 0.0f, 10.0f,  "%.2f");
							ImGui::SliderFloat("波長 (m)",           &params->waves[i].wavelength,1.0f, 200.0f, "%.1f");
							ImGui::SliderFloat("速度 (m/s)",         &params->waves[i].speed,     0.0f, 20.0f,  "%.2f");
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
				}

				// --- Fresnel ---
				if (ImGui::CollapsingHeader("Fresnel"))
				{
					ImGui::SliderFloat("バイアス",  &params->fresnelBias,  0.0f, 1.0f,  "%.3f");
					ImGui::SliderFloat("スケール",  &params->fresnelScale, 0.0f, 2.0f,  "%.3f");
					ImGui::SliderFloat("累乗",      &params->fresnelPower, 1.0f, 10.0f, "%.1f");
				}

				// --- 太陽ハイライト ---
				if (ImGui::CollapsingHeader("太陽ハイライト"))
				{
					ImGui::SliderFloat("光沢度 (Shininess)", &params->sunShininess, 8.0f,  2048.0f, "%.0f");
					ImGui::SliderFloat("強度 (Intensity)",   &params->sunIntensity, 0.0f,  10.0f,   "%.2f");
					ImGui::ColorEdit3("空の反射色",            &params->skyColor.x);
				}

				// --- 海の色 ---
				if (ImGui::CollapsingHeader("海の色"))
				{
					ImGui::ColorEdit3("深い色 (deep)",   &params->deepColor.x);
					ImGui::ColorEdit3("浅い色 (shallow)", &params->shallowColor.x);
				}

				// --- UV スクロール法線マップ ---
				if (ImGui::CollapsingHeader("UV スクロール (法線マップ)"))
				{
					ImGui::PushID("nm1");
					ImGui::Text("法線マップ 1");
					ImGui::SliderFloat("スケール",      &params->normalScale1, 0.1f, 10.0f, "%.2f");
					// normalDirX1・normalDirZ1 は隣接しているため SliderFloat2 で一括編集
					ImGui::SliderFloat2("方向 (XZ)",    &params->normalDirX1,  -1.0f, 1.0f, "%.2f");
					ImGui::SliderFloat("速度",          &params->normalSpeed1,  0.0f, 0.5f, "%.4f");
					ImGui::PopID();

					ImGui::Separator();

					ImGui::PushID("nm2");
					ImGui::Text("法線マップ 2");
					ImGui::SliderFloat("スケール",      &params->normalScale2, 0.1f, 10.0f, "%.2f");
					ImGui::SliderFloat2("方向 (XZ)",    &params->normalDirX2,  -1.0f, 1.0f, "%.2f");
					ImGui::SliderFloat("速度",          &params->normalSpeed2,  0.0f, 0.5f, "%.4f");
					ImGui::PopID();
				}
			}
			ImGui::End();
		}
	}
}
#endif
