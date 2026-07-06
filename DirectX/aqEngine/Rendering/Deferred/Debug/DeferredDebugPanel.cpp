#include "aq.h"
#include "DeferredDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Shadow/IShadowRenderer.h"
#include "Graphics/IRenderTarget.h"
#include <imgui/imgui.h>


namespace aq
{
	namespace rendering
	{
		DeferredDebugPanel::DeferredDebugPanel(DeferredRenderer& deferred, IShadowRenderer* shadow)
			: deferred_(deferred)
			, shadow_(shadow)
		{
		}


		void DeferredDebugPanel::RenderContent()
		{
			ImGui::SliderFloat("Preview Size", &previewSize_, 80.0f, 400.0f, "%.0f");
			ImGui::Separator();

			auto& gd = graphics::GraphicsDevice::Get();
			const ImVec2 sz(previewSize_, previewSize_);

			// GBuffer0: baseColor.rgb + metallic  (PBR)
			auto* gb0 = gd.GetRenderTarget(deferred_.GetGBuffer0Handle());
			// GBuffer1: N.xyz + roughness  (PBR)
			auto* gb1 = gd.GetRenderTarget(deferred_.GetGBuffer1Handle());
			// GBuffer2: worldPos.xyz + specular(F0)  (PBR)
			auto* gb2 = gd.GetRenderTarget(deferred_.GetGBuffer2Handle());
			// GBuffer3: emissive*scale + pixelTag
			auto* gb3 = gd.GetRenderTarget(deferred_.GetGBuffer3Handle());

			// 上段: GBuffer0, GBuffer1
			if (gb0)
			{
				ImGui::Image((ImTextureID)gb0->GetRenderTargetSRV().GetNativeHandle(), sz);
				ImGui::SameLine();
			}
			if (gb1)
			{
				ImGui::Image((ImTextureID)gb1->GetRenderTargetSRV().GetNativeHandle(), sz);
			}

			// 上段ラベル
			if (gb0)
			{
				ImGui::Text("GBuffer0: BaseColor (RGB) + Metallic (A)");
				ImGui::SameLine(previewSize_ + 8.0f);
			}
			ImGui::Text("GBuffer1: Normal (RGB) + Roughness (A)");

			ImGui::Spacing();

			// 下段: GBuffer2, GBuffer3
			if (gb2)
			{
				ImGui::Image((ImTextureID)gb2->GetRenderTargetSRV().GetNativeHandle(), sz);
				ImGui::SameLine();
			}
			if (gb3)
			{
				ImGui::Image((ImTextureID)gb3->GetRenderTargetSRV().GetNativeHandle(), sz);
			}

			// 下段ラベル
			if (gb2)
			{
				ImGui::Text("GBuffer2: WorldPos (RGB) + Specular/F0 (A)");
				ImGui::SameLine(previewSize_ + 8.0f);
			}
			ImGui::Text("GBuffer3: Emissive (RGB) + PixelTag (A)");

			// シャドウデプスマップ
			if (shadow_)
			{
				ImGui::Separator();
				auto* depthMap = shadow_->GetDepthMap();
				if (depthMap && depthMap->GetSRV())
				{
					ImGui::Image((ImTextureID)depthMap->GetSRV()->GetNativeHandle(), sz);
					ImGui::Text("Shadow Depth Map (%u x %u)", depthMap->GetResolution(), depthMap->GetResolution());
				}
			}
		}
	}
}
#endif
