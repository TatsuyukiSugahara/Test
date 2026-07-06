#include "aq.h"
#include "TextStyleEditorPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include "UI/Font/TextStyleCache.h"
#include <cstdio>

namespace aq
{
	namespace ui
	{
		// =========================================================================
		// メニュー
		// =========================================================================

		void TextStyleEditorPanel::DebugRenderMenu()
		{
			if (ImGui::MenuItem("TextStyle Editor")) show_ = !show_;
		}


		// =========================================================================
		// スタイルリスト & プロパティ
		// =========================================================================

		void TextStyleEditorPanel::DebugRender()
		{
			if (!show_) return;

			ImGui::SetNextWindowSize(ImVec2(480.f, 720.f), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("TextStyle Editor", &show_))
			{
				ImGui::End();
				return;
			}

			RenderStyleList();
			ImGui::Separator();
			RenderStyleProperties();
			ImGui::Separator();
			RenderPreview();

			ImGui::End();
		}


		void TextStyleEditorPanel::RenderStyleList()
		{
			ImGui::Text("File:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(300.f);
			ImGui::InputText("##path", pathBuf_, sizeof(pathBuf_));
			ImGui::SameLine();
			if (ImGui::Button("Load"))
				LoadStyle(pathBuf_);
			ImGui::SameLine();
			if (ImGui::Button("New"))
			{
				current_ = TextStyle::MakeDefault();
				statusMsg_[0] = '\0';
			}

			if (statusMsg_[0])
			{
				ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "%s", statusMsg_);
			}
		}


		void TextStyleEditorPanel::RenderStyleProperties()
		{
			ImGui::PushItemWidth(200.f);

			// --- 基本情報 ---
			{
				char buf[256];
				strncpy_s(buf, sizeof(buf), current_.name.c_str(), _TRUNCATE);
				if (ImGui::InputText("Name##style", buf, sizeof(buf)))
					current_.name = buf;

				strncpy_s(buf, sizeof(buf), current_.fontPath.c_str(), _TRUNCATE);
				if (ImGui::InputText("Font (atlas.json)##style", buf, sizeof(buf)))
					current_.fontPath = buf;

				ImGui::DragFloat("Font Size##style", &current_.fontSize, 0.5f, 4.f, 256.f, "%.1f px");
			}

			// --- Fill ---
			if (ImGui::CollapsingHeader("Fill", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::ColorEdit4("Fill Color##style", &current_.fillColor.x);
			}

			// --- Outline ---
			if (ImGui::CollapsingHeader("Outline"))
			{
				ImGui::Checkbox("Enable##outline", &current_.outline.enabled);
				ImGui::BeginDisabled(!current_.outline.enabled);
				ImGui::ColorEdit4("Color##outline",  &current_.outline.color.x);
				ImGui::SliderFloat("Width##outline", &current_.outline.width, 0.f, 0.5f, "%.3f");
				ImGui::EndDisabled();
			}

			// --- Shadow ---
			if (ImGui::CollapsingHeader("Shadow"))
			{
				ImGui::Checkbox("Enable##shadow", &current_.shadow.enabled);
				ImGui::BeginDisabled(!current_.shadow.enabled);
				ImGui::ColorEdit4("Color##shadow",     &current_.shadow.color.x);
				ImGui::DragFloat2("Offset##shadow",    &current_.shadow.offset.x, 0.5f, -100.f, 100.f, "%.1f px");
				ImGui::SliderFloat("Softness##shadow", &current_.shadow.softness,  0.f, 0.5f, "%.3f");
				ImGui::EndDisabled();
			}

			// --- Gradient ---
			if (ImGui::CollapsingHeader("Gradient"))
			{
				ImGui::Checkbox("Enable##grad", &current_.gradient.enabled);
				ImGui::BeginDisabled(!current_.gradient.enabled);
				ImGui::ColorEdit4("Top Color##grad",    &current_.gradient.topColor.x);
				ImGui::ColorEdit4("Bottom Color##grad", &current_.gradient.bottomColor.x);
				ImGui::EndDisabled();
			}

			ImGui::PopItemWidth();

			ImGui::Separator();

			// --- Save ---
			if (ImGui::Button("Save"))
				SaveCurrent();
		}


		// =========================================================================
		// プレビュー
		// =========================================================================

		void TextStyleEditorPanel::RenderPreview()
		{
			ImGui::TextDisabled("--- Preview ---");
			ImGui::SetNextItemWidth(-1.f);
			ImGui::InputText("##preview_text", previewBuf_, sizeof(previewBuf_));
			ImGui::TextDisabled("(ImGui font による近似表示)");

			if (previewBuf_[0] == '\0') return;

			ImFont*     font     = ImGui::GetFont();
			const float fontSize = current_.fontSize > 0.f ? current_.fontSize : ImGui::GetFontSize();

			ImDrawList* dl  = ImGui::GetWindowDrawList();
			const float avW = ImGui::GetContentRegionAvail().x;
			const float boxH = fontSize * 2.5f;
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const ImVec2 p1 = ImVec2(p0.x + avW, p0.y + boxH);

			// 背景
			dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, 255), 4.f);

			// テキスト描画の基準位置 (中央揃え近似)
			const ImVec2 textSz = font->CalcTextSizeA(fontSize, FLT_MAX, avW, previewBuf_);
			const float  tx     = p0.x + (avW - textSz.x) * 0.5f;
			const float  ty     = p0.y + (boxH - textSz.y) * 0.5f;

			// shadow 近似 (オフセットして描画)
			if (current_.shadow.enabled)
			{
				const auto& sc  = current_.shadow.color;
				const ImU32 shC = IM_COL32(
					static_cast<int>(sc.x * 255), static_cast<int>(sc.y * 255),
					static_cast<int>(sc.z * 255), static_cast<int>(sc.w * 255));
				// Y 軸は ImGui が下向き正なので反転
				const float ox = current_.shadow.offset.x * 0.5f;
				const float oy = -current_.shadow.offset.y * 0.5f;
				dl->AddText(font, fontSize, ImVec2(tx + ox, ty + oy), shC, previewBuf_);
			}

			// outline 近似: 上下左右 + 斜め計 8 方向で自然に見える
			if (current_.outline.enabled)
			{
				const auto& oc   = current_.outline.color;
				const ImU32 outC = IM_COL32(
					static_cast<int>(oc.x * 255), static_cast<int>(oc.y * 255),
					static_cast<int>(oc.z * 255), static_cast<int>(oc.w * 255));
				const float d  = 1.f + current_.outline.width * fontSize * 0.15f;
				const float d2 = d * 0.707f; // 斜め方向
				dl->AddText(font, fontSize, ImVec2(tx - d,  ty     ), outC, previewBuf_);
				dl->AddText(font, fontSize, ImVec2(tx + d,  ty     ), outC, previewBuf_);
				dl->AddText(font, fontSize, ImVec2(tx,      ty - d ), outC, previewBuf_);
				dl->AddText(font, fontSize, ImVec2(tx,      ty + d ), outC, previewBuf_);
				dl->AddText(font, fontSize, ImVec2(tx - d2, ty - d2), outC, previewBuf_);
				dl->AddText(font, fontSize, ImVec2(tx + d2, ty - d2), outC, previewBuf_);
				dl->AddText(font, fontSize, ImVec2(tx - d2, ty + d2), outC, previewBuf_);
				dl->AddText(font, fontSize, ImVec2(tx + d2, ty + d2), outC, previewBuf_);
			}

			// fill
			if (current_.gradient.enabled)
			{
				// AddText は単色のみなので水平ストリップに分割して lerp color で描画
				constexpr int STRIPS = 16;
				const auto& tc = current_.gradient.topColor;
				const auto& bc = current_.gradient.bottomColor;
				for (int i = 0; i < STRIPS; ++i)
				{
					const float t  = (i + 0.5f) / STRIPS;
					const ImU32 sc = IM_COL32(
						static_cast<int>((tc.x + (bc.x - tc.x) * t) * 255),
						static_cast<int>((tc.y + (bc.y - tc.y) * t) * 255),
						static_cast<int>((tc.z + (bc.z - tc.z) * t) * 255),
						static_cast<int>((tc.w + (bc.w - tc.w) * t) * 255));
					const float clipY0 = ty + textSz.y * (static_cast<float>(i)     / STRIPS);
					const float clipY1 = ty + textSz.y * (static_cast<float>(i + 1) / STRIPS);
					dl->PushClipRect(ImVec2(p0.x, clipY0), ImVec2(p1.x, clipY1), true);
					dl->AddText(font, fontSize, ImVec2(tx, ty), sc, previewBuf_);
					dl->PopClipRect();
				}
			}
			else
			{
				const auto& fc = current_.fillColor;
				const ImU32 fillC = IM_COL32(
					static_cast<int>(fc.x * 255), static_cast<int>(fc.y * 255),
					static_cast<int>(fc.z * 255), static_cast<int>(fc.w * 255));
				dl->AddText(font, fontSize, ImVec2(tx, ty), fillC, previewBuf_);
			}

			ImGui::Dummy(ImVec2(avW, boxH));
		}


		// =========================================================================
		// ロード / セーブ
		// =========================================================================

		void TextStyleEditorPanel::LoadStyle(const std::string& path)
		{
			if (current_.LoadFromJson(path.c_str()))
			{
				// キャッシュを無効化して次回ロード時に最新を使うようにする
				TextStyleCache::Get().Invalidate(path);
				std::snprintf(statusMsg_, sizeof(statusMsg_), "Loaded: %s", path.c_str());
			}
			else
			{
				std::snprintf(statusMsg_, sizeof(statusMsg_), "Failed to load: %s", path.c_str());
			}
		}

		void TextStyleEditorPanel::SaveCurrent()
		{
			if (current_.SaveToJson(pathBuf_))
			{
				// キャッシュを無効化して次フレームから新しいスタイルが反映される
				TextStyleCache::Get().Invalidate(pathBuf_);
				std::snprintf(statusMsg_, sizeof(statusMsg_), "Saved: %s", pathBuf_);
			}
			else
			{
				std::snprintf(statusMsg_, sizeof(statusMsg_), "Failed to save: %s", pathBuf_);
			}
		}

	} // namespace ui
} // namespace aq
#endif
