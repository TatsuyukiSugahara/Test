#include "aq.h"
#include "HiZDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include "HiZRenderer.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/IRenderTarget.h"
#include "Graphics/Camera.h"
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
				"Hi-Z ピラミッド: GBuffer2 worldPos から再構成した深度の max ミップ連鎖。"
				"白いほど遠い (depth=1)。背景は遠方扱い。");
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
						"Readback: まだ届いていません (GPU→CPU 失敗 or 起動直後)");
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
						ImGui::TextDisabled("Self-test: 有効な検証画素なし (画面に近距離の不透明物が必要)");
					}
					else
					{
						ImGui::TextColored(st.pass ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
							"Self-test: %s  (depth %.3f / 手前=%s 奥=%s)",
							st.pass ? "PASS" : "FAIL",
							st.sampleDepth,
							st.nearOccluded ? "遮蔽" : "可視",
							st.farOccluded  ? "遮蔽" : "可視");
						ImGui::TextDisabled("PASS = 手前が可視 かつ 奥が遮蔽 → 遮蔽判定は正常に動作");
					}
				}
			}
			ImGui::Separator();

			auto& gd = graphics::GraphicsDevice::Get();
			const uint32_t count = renderer_.GetLevelCount();
			if (count == 0)
			{
				ImGui::TextDisabled("(Hi-Z 未初期化)");
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
