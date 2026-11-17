#include "aq.h"
#include "ShadowDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include "Rendering/Shadow/IShadowRenderer.h"
#include "Graphics/IDepthMap.h"

namespace aq
{
	namespace rendering
	{
		ShadowDebugPanel::ShadowDebugPanel(ShadowSettings& settings, IShadowRenderer* renderer)
			: settings_(settings)
			, renderer_(renderer)
		{
		}


		void ShadowDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("Shadow", nullptr, &show_);
		}


		void ShadowDebugPanel::RenderContent()
		{
			// ---- 設定スライダー ------------------------------------------------
			ImGui::SliderFloat("Depth Bias",   &settings_.depthBias,   0.0f,  0.05f,  "%.4f");
			ImGui::SliderFloat("PCF Softness", &settings_.softness,    0.0f,  8.0f,   "%.2f");
			ImGui::SliderFloat("Ortho Width",  &settings_.orthoWidth,  10.0f, 200.0f, "%.1f");
			ImGui::SliderFloat("Ortho Height", &settings_.orthoHeight, 10.0f, 200.0f, "%.1f");
			ImGui::SliderFloat("Far Plane",    &settings_.farPlane,    10.0f, 500.0f, "%.1f");
			ImGui::Separator();
			const auto& c = settings_.sceneCenter;
			ImGui::Text("Scene Center  (%.2f, %.2f, %.2f)", c.x, c.y, c.z);

			// ---- シャドウマップ プレビュー ------------------------------------
			if (!renderer_) return;
			graphics::IDepthMap* depthMap = renderer_->GetDepthMap();
			if (!depthMap) return;

			ImGui::Separator();
			ImGui::SliderFloat("Preview Size", &previewSize_, 80.0f, 400.0f, "%.0f");

			const uint32_t lightCount =
				graphics::LightManager::Get().GetDirectionalLightCount();
			const ImVec2 sz(previewSize_, previewSize_);

			static const char* kLabels[] = {
				"Light 0 (Shadow)", "Light 1", "Light 2", "Light 3"
			};

			// E1a: スライス SRV は TEXTURE2DARRAY(ArraySize=1 の配列ビュー)のままで、
			// 公式 ImGui バックエンドのシェーダーは Texture2D しか読めない。配列を
			// 2D のプレビュー RT に写す対応(エディタ全体設計.md 15 章)までは画像表示を
			// やめ、解像度だけを出す(D3D11/D3D12/Vulkan 共通)。
			for (uint32_t i = 0; i < lightCount; ++i)
			{
				auto* srv = depthMap->GetSliceSRV(i);
				if (!srv) continue;

				ImGui::PushID(static_cast<int>(i));

				if (i % 2 == 1) ImGui::SameLine();

				ImGui::BeginGroup();
				ImGui::Dummy(sz);
				ImGui::Text("シャドウ深度マップの画像表示は未対応");
				ImGui::Text("%u x %u", depthMap->GetResolution(), depthMap->GetResolution());
				ImGui::Text("%s", (i < 4) ? kLabels[i] : "Light");
				ImGui::EndGroup();

				ImGui::PopID();
			}
		}


		void ShadowDebugPanel::DebugRender()
		{
			if (!show_) return;
			if (ImGui::Begin("Shadow"))
				RenderContent();
			ImGui::End();
		}
	}
}
#endif
