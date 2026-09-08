#include "aq.h"
#include "HiZDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Occlusion/HiZRenderer.h"
#include "Graphics/IRenderTarget.h"
#include <imgui/imgui.h>


namespace aq
{
	namespace rendering
	{
		HiZDebugPanel::HiZDebugPanel(HiZRenderer& renderer)
			: renderer_(renderer)
		{
		}


		void HiZDebugPanel::RenderContent()
		{
			ImGui::TextWrapped(
				"Hi-Z pyramid: max-depth mip chain reconstructed from GBuffer2 worldPos. "
				"Brighter = farther (depth=1). Background counts as far.");
			ImGui::SliderFloat("Preview Size", &previewSize_, 80.0f, 360.0f, "%.0f");
			ImGui::Separator();

			// --- 遮蔽の動作確認 ---
			{
				const HiZRenderer::ReadbackInfo rb = renderer_.GetReadbackInfo();
				if (rb.hasData)
				{
					ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
						"Readback: OK  %ux%u  depth[min %.3f, max %.3f]",
						rb.width, rb.height, rb.minV, rb.maxV);
				}
				else
				{
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
						"Readback: not available yet (GPU->CPU failed or just started)");
				}
				// 診断カウンタ (createFails スロットは readFound, valid スロットは Map HRESULT を流用)
				uint32_t copies = 0, maps = 0, readFound = 0, stamps = 0, mapHr = 0, fenceReady = 0;
				graphics::GraphicsDevice::Get().GetReadbackDebug(copies, maps, readFound, stamps, mapHr, fenceReady);
				ImGui::TextDisabled("diag: copies=%u maps=%u readFound=%u stamps=%u mapHr=0x%08X fenceReady=%u",
					copies, maps, readFound, stamps, mapHr, fenceReady);

				// 遮蔽判定の自己テスト (シーンに遮蔽が無くても数式の正しさを確認)
				auto* cam = CameraManager::Get().GetCamera(CameraType::Main);
				if (cam && rb.hasData)
				{
					const math::Matrix4x4 vp = cam->GetViewProjectionMatrix();
					const math::Matrix4x4& vi = cam->GetViewMatrixInverse();
					math::Vector3 fwd(vi._31, vi._32, vi._33);
					fwd.Normalize();
					const HiZRenderer::SelfTestResult st =
						renderer_.SelfTest(vp, cam->GetPosition(), fwd, cam->GetNear(), cam->GetFar());
					if (!st.valid)
					{
						ImGui::TextDisabled("Self-test: no valid test pixels (needs a nearby opaque object on screen)");
					}
					else
					{
						ImGui::TextColored(st.pass ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
							"Self-test: %s  (depth %.3f / near=%s far=%s)",
							st.pass ? "PASS" : "FAIL",
							st.sampleDepth,
							st.nearOccluded ? "occluded" : "visible",
							st.farOccluded  ? "occluded" : "visible");
						ImGui::TextDisabled("PASS = near visible AND far occluded -> occlusion test works");
					}
				}
			}
			ImGui::Separator();

			auto& gd = graphics::GraphicsDevice::Get();
			const uint32_t count = renderer_.GetLevelCount();
			if (count == 0)
			{
				ImGui::TextDisabled("(Hi-Z not initialized)");
				return;
			}

			float lineW = 0.0f;
			const float avail = ImGui::GetContentRegionAvail().x;
			for (uint32_t i = 0; i < count; ++i)
			{
				auto* rt = gd.GetRenderTarget(renderer_.GetLevelHandle(i));
				if (!rt) continue;

				uint32_t w, h;
				renderer_.GetLevelSize(i, w, h);

				ImGui::BeginGroup();
				ImGui::Image((ImTextureID)rt->GetRenderTargetSRV().GetNativeHandle(),
				             ImVec2(previewSize_, previewSize_));
				ImGui::Text("L%u  %ux%u", i, w, h);
				ImGui::EndGroup();

				// 横幅が許す限り横並び
				lineW += previewSize_ + 8.0f;
				if (lineW + previewSize_ < avail && i + 1 < count)
					ImGui::SameLine();
				else
					lineW = 0.0f;
			}
		}
	}
}
#endif
