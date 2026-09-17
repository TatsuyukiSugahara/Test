#include "aq.h"
#include "UIEditorDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include <algorithm>
#include "UIEditorSession.h"
#include "UI/UIObject.h"
#include "UI/Screen/UIScreen.h"
#include "UI/Screen/UIScreenManager.h"
#include "UI/Component/UITransformComponent.h"
#include "UI/Component/UIImageComponent.h"
#include "UI/Component/UICanvasComponent.h"
#include "UI/Component/UINineSliceComponent.h"
#include "UI/Component/UICircleGaugeComponent.h"
#include "UI/Component/UIButtonComponent.h"
#include "UI/Component/UITextComponent.h"
#include "UI/Component/UIAnimationComponent.h"
#include "UI/Font/TextStyleCache.h"
#include "UI/Resource/UIDocumentSerializer.h"
#include <cstdio>

namespace aq
{
	namespace ui
	{
		// ---- テクスチャ非同期ロードラッパー ---------------------------------
		namespace
		{
			class DeferredSRV final : public graphics::IShaderResourceView
			{
			public:
				explicit DeferredSRV(std::shared_ptr<res::GPUResource> r) : res_(std::move(r)) {}
				void  Release() override {}
				void* GetNativeHandle() const override
				{
					if (!res_) return nullptr;
					auto* srv = res_->GetShaderResourceView();
					return srv ? srv->GetNativeHandle() : nullptr;
				}
			private:
				std::shared_ptr<res::GPUResource> res_;
			};

			std::shared_ptr<graphics::IShaderResourceView> LoadTexture(const char* path)
			{
				if (!path || path[0] == '\0') return nullptr;
				auto gpuRes = res::ResourceManager::Get().Load<res::GPUResource>(path);
				if (!gpuRes) return nullptr;
				return std::make_shared<DeferredSRV>(std::move(gpuRes));
			}

			// ImGui ウィジェット編集直後に呼ぶと dirty を立てる
			void MarkDirtyIfEdited()
			{
				if (ImGui::IsItemEdited())
					UIEditorSession::Get().dirty = true;
			}
		} // anonymous namespace


		// ---- アンカープリセットピッカー ------------------------------------
		void UIEditorDebugPanel::RenderAnchorPicker(UITransformComponent* tc)
		{
			struct Preset { const char* label; float minX, minY, maxX, maxY; };
			static const Preset GRID[9] = {
				{"↖", 0.f,  0.f,  0.f,  0.f },
				{"↑", .5f,  0.f,  .5f,  0.f },
				{"↗", 1.f,  0.f,  1.f,  0.f },
				{"←", 0.f,  .5f,  0.f,  .5f },
				{"●", .5f,  .5f,  .5f,  .5f },
				{"→", 1.f,  .5f,  1.f,  .5f },
				{"↙", 0.f,  1.f,  0.f,  1.f },
				{"↓", .5f,  1.f,  .5f,  1.f },
				{"↘", 1.f,  1.f,  1.f,  1.f },
			};
			static const Preset STRETCH[3] = {
				{"←→", 0.f, .5f, 1.f, .5f},
				{" ↕ ", .5f, 0.f, .5f, 1.f},
				{"All", 0.f, 0.f, 1.f, 1.f},
			};

			ImGui::TextDisabled("Anchor");
			const ImVec2 btnSz = {30.f, 22.f};
			for (int i = 0; i < 9; ++i)
			{
				if (i % 3 != 0) ImGui::SameLine(0.f, 2.f);
				ImGui::PushID(i);
				const bool active = (tc->anchor.min.x == GRID[i].minX &&
				                     tc->anchor.min.y == GRID[i].minY &&
				                     tc->anchor.max.x == GRID[i].maxX &&
				                     tc->anchor.max.y == GRID[i].maxY);
				if (active)
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				if (ImGui::Button(GRID[i].label, btnSz))
				{
					tc->anchor.min = {GRID[i].minX, GRID[i].minY};
					tc->anchor.max = {GRID[i].maxX, GRID[i].maxY};
					UIEditorSession::Get().dirty = true;
				}
				if (active) ImGui::PopStyleColor();
				ImGui::PopID();
			}
			ImGui::SameLine(0.f, 8.f);
			ImGui::TextDisabled("|");
			for (int i = 0; i < 3; ++i)
			{
				ImGui::SameLine(0.f, 4.f);
				ImGui::PushID(100 + i);
				const bool active = (tc->anchor.min.x == STRETCH[i].minX &&
				                     tc->anchor.min.y == STRETCH[i].minY &&
				                     tc->anchor.max.x == STRETCH[i].maxX &&
				                     tc->anchor.max.y == STRETCH[i].maxY);
				if (active)
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				if (ImGui::Button(STRETCH[i].label, {38.f, 22.f}))
				{
					tc->anchor.min = {STRETCH[i].minX, STRETCH[i].minY};
					tc->anchor.max = {STRETCH[i].maxX, STRETCH[i].maxY};
					UIEditorSession::Get().dirty = true;
				}
				if (active) ImGui::PopStyleColor();
				ImGui::PopID();
			}
		}


		// ---- テクスチャパス入力ウィジェット (Image/NineSlice/CircleGauge 共通) --
		namespace
		{
			void RenderTexturePicker(
				const char* id,
				char* pathBuf,
				size_t pathBufSize,
				std::vector<std::shared_ptr<graphics::IShaderResourceView>>& loadedTextures,
				std::string& texturePath,
				std::shared_ptr<graphics::IShaderResourceView>& outTexture)
			{
				ImGui::PushID(id);
				ImGui::InputText("##texpath", pathBuf, pathBufSize);
				ImGui::SameLine();
				if (ImGui::Button("Load Tex"))
				{
					auto srv = LoadTexture(pathBuf);
					if (srv)
					{
						loadedTextures.push_back(srv);
						outTexture  = srv;
						texturePath = pathBuf;
						UIEditorSession::Get().dirty = true;
					}
				}
				ImGui::TextDisabled("ex) Assets/UI/xxx.png");
				ImGui::PopID();
			}
		} // anonymous namespace


		// ---- ツリービュー ---------------------------------------------------
		void UIEditorDebugPanel::RenderTree(UIObject* node)
		{
			if (!node) return;

			auto& session = UIEditorSession::Get();

			const bool isLeaf = node->GetChildren().empty();
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
			                        | ImGuiTreeNodeFlags_SpanAvailWidth
			                        | ImGuiTreeNodeFlags_DefaultOpen;
			if (isLeaf)   flags |= ImGuiTreeNodeFlags_Leaf;
			if (session.selectedObject == node->GetHandle()) flags |= ImGuiTreeNodeFlags_Selected;

			const bool nameEmpty = node->GetName().empty();
			const char* label = nameEmpty ? "(no name)" : node->GetName().data();

			ImGui::PushID(node);
			const bool open = ImGui::TreeNodeEx(label, flags);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				session.selectedObject = node->GetHandle();
			if (open)
			{
				for (UIObject* child : node->GetChildren())
					RenderTree(child);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}


		// ---- ツールバー (Save / Save As / Reload) ----------------------------
		void UIEditorDebugPanel::RenderToolbar(UIObject* root)
		{
			auto& session = UIEditorSession::Get();

			ImGui::Text("Screen: %s", session.screenName.empty() ? "(none)" : session.screenName.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", session.documentPath.empty() ? "(unregistered)" : session.documentPath.c_str());

			ImGui::SameLine();
			ImGui::BeginDisabled(root == nullptr || session.documentPath.empty());
			if (ImGui::Button("Save"))
			{
				std::vector<std::string> errors;
				if (!UIAnimationEditor::ValidateTree(root, errors))
				{
					std::snprintf(statusMsg_, sizeof(statusMsg_),
					    "Save blocked: %zu animation error(s) (see Animation tab)", errors.size());
				}
				else
				{
					const bool ok = UIDocumentSerializer::Save(root, session.documentPath);
					if (ok) session.dirty = false;
					std::snprintf(statusMsg_, sizeof(statusMsg_),
					    ok ? "Saved: %s" : "Save FAILED: %s", session.documentPath.c_str());
				}
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::BeginDisabled(root == nullptr);
			if (ImGui::Button("Save As"))
			{
				std::snprintf(saveAsPathBuf_, sizeof(saveAsPathBuf_), "%s", session.documentPath.c_str());
				ImGui::OpenPopup("Save As##uieditor");
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("Reload"))
			{
				if (session.dirty)
					ImGui::OpenPopup("Reload Confirm##uieditor");
				else
				{
					ClearForReload();
					UIContext::Get().Screens().ReloadTopDocument();
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(ref nodes are expanded on save)");

			// --- Save As ポップアップ ---
			if (ImGui::BeginPopup("Save As##uieditor"))
			{
				ImGui::SetNextItemWidth(360.f);
				ImGui::InputText("##saveaspath", saveAsPathBuf_, sizeof(saveAsPathBuf_));
				ImGui::SameLine();
				if (ImGui::Button("Save##saveas") && root && saveAsPathBuf_[0] != '\0')
				{
					std::vector<std::string> errors;
					if (!UIAnimationEditor::ValidateTree(root, errors))
					{
						std::snprintf(statusMsg_, sizeof(statusMsg_),
						    "Save blocked: %zu animation error(s) (see Animation tab)", errors.size());
					}
					else
					{
						const bool ok = UIDocumentSerializer::Save(root, saveAsPathBuf_);
						std::snprintf(statusMsg_, sizeof(statusMsg_),
						    ok ? "Saved: %s" : "Save FAILED: %s", saveAsPathBuf_);
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			// --- Reload 未保存確認モーダル ---
			if (ImGui::BeginPopupModal("Reload Confirm##uieditor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("Unsaved changes will be lost. Reload anyway?");
				if (ImGui::Button("Reload", {120.f, 0.f}))
				{
					ClearForReload();
					UIContext::Get().Screens().ReloadTopDocument();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", {120.f, 0.f}))
					ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}

			if (statusMsg_[0] != '\0')
				ImGui::TextDisabled("%s", statusMsg_);
		}


		// ---- Reload 直前のエディタ状態クリア ---------------------------------
		void UIEditorDebugPanel::ClearForReload()
		{
			auto& session = UIEditorSession::Get();
			session.ClearSelection();
			session.dirty = false;   // 再生成後のドキュメントは保存済み状態から始める

			animationEditor_.Reset();

			prevSelectedHandle_          = UIObjectHandle::Invalid();
			imageTexPathBuf_[0]          = '\0';
			nineSliceTexPathBuf_[0]      = '\0';
			circleGaugeTexPathBuf_[0]    = '\0';
			textContentBuf_[0]           = '\0';
			textStyleBuf_[0]             = '\0';
			nameBuf_[0]                  = '\0';
			statusMsg_[0]                = '\0';
		}


		// ---- プロパティペイン -----------------------------------------------
		void UIEditorDebugPanel::RenderProperties(UIObject* obj)
		{
			auto& ctx     = UIContext::Get();
			auto& session = UIEditorSession::Get();

			// --- ツールバー (Add / Delete) ---
			{
				UIScreen* top  = ctx.Screens().Top();
				UIObject* root = top ? top->GetRoot() : nullptr;

				if (ImGui::Button("+ Add Child"))
				{
					UIObject* newObj = ctx.CreateObject("New Object");
					newObj->AddComponent<UITransformComponent>();
					UIObject* attachTo = obj ? obj : root;
					if (attachTo)
						attachTo->AddChild(newObj);
					session.dirty = true;
				}
				ImGui::SameLine();

				const bool canDelete = obj && obj->GetParent() != nullptr;
				if (!canDelete) ImGui::BeginDisabled();
				if (ImGui::Button("Delete"))
				{
					// 破棄する前に Animation Editor 側の参照 (プレビュー等) を後始末させる
					animationEditor_.OnTargetChanged(obj);
					ctx.DestroyObject(session.selectedObject);
					session.ClearSelection();
					session.dirty = true;
					obj = nullptr;
				}
				if (!canDelete) ImGui::EndDisabled();

				// コンポーネント追加メニュー
				ImGui::SameLine();
				if (obj && ImGui::BeginMenu("+ Component"))
				{
					// 描画コンポーネント (Image / Nine Slice / Circle Gauge) は 1 つまで (設計書 §2.1)
					if (!obj->HasComponent<UIImageComponent>() && !obj->HasRenderComponent() &&
					    ImGui::MenuItem("Image"))
					{
						obj->AddComponent<UIImageComponent>();
						session.dirty = true;
					}

					if (!obj->HasComponent<UINineSliceComponent>() && !obj->HasRenderComponent() &&
					    ImGui::MenuItem("Nine Slice"))
					{
						obj->AddComponent<UINineSliceComponent>();
						session.dirty = true;
					}

					if (!obj->HasComponent<UICircleGaugeComponent>() && !obj->HasRenderComponent() &&
					    ImGui::MenuItem("Circle Gauge"))
					{
						obj->AddComponent<UICircleGaugeComponent>();
						session.dirty = true;
					}

					if (!obj->HasComponent<UIButtonComponent>() &&
					    ImGui::MenuItem("Button"))
					{
						obj->AddComponent<UIButtonComponent>();
						session.dirty = true;
					}

					if (!obj->HasComponent<UITextComponent>() &&
					    ImGui::MenuItem("Text"))
					{
						obj->AddComponent<UITextComponent>();
						session.dirty = true;
					}

					if (!obj->HasComponent<UICanvasComponent>() &&
					    ImGui::MenuItem("Canvas"))
					{
						obj->AddComponent<UICanvasComponent>();
						session.dirty = true;
					}

					ImGui::EndMenu();
				}
			}

			if (!obj)
			{
				ImGui::Separator();
				ImGui::TextDisabled("Select an object");
				return;
			}

			ImGui::Separator();

			// --- 名前 ---
			if (ImGui::InputText("Name", nameBuf_, sizeof(nameBuf_),
				ImGuiInputTextFlags_EnterReturnsTrue))
			{
				obj->SetName(nameBuf_);
				session.dirty = true;
			}

			// --- Inspector タブ (Properties / Animation) ---
			if (ImGui::BeginTabBar("##inspector"))
			{
				if (ImGui::BeginTabItem("Properties"))
				{
					inspectorTab_ = InspectorTab::Properties;
					RenderPropertiesTab(obj);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Animation"))
				{
					inspectorTab_ = InspectorTab::Animation;

					if (obj->GetComponent<UIAnimationComponent>())
					{
						animationEditor_.DrawAnimationTab(obj);
					}
					else
					{
						ImGui::TextDisabled("This object has no UIAnimationComponent.");
						if (ImGui::Button("Add Animation"))
						{
							obj->AddComponent<UIAnimationComponent>();
							session.dirty = true;
						}
					}
					ImGui::EndTabItem();
				}

				ImGui::EndTabBar();
			}
		}


		// ---- Inspector: Properties タブの中身 --------------------------------
		void UIEditorDebugPanel::RenderPropertiesTab(UIObject* obj)
		{
			auto& session = UIEditorSession::Get();

			// --- UITransformComponent ---
			if (auto* tc = obj->GetComponent<UITransformComponent>())
			{
				if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::DragFloat3("Position", &tc->localPosition.x, 1.f);
					MarkDirtyIfEdited();
					ImGui::DragFloat2("Size",      &tc->sizeDelta.x,    1.f, 0.f, 4096.f);
					MarkDirtyIfEdited();
					ImGui::DragFloat("Rotation",   &tc->rotation,       0.5f, -360.f, 360.f);
					MarkDirtyIfEdited();
					ImGui::DragFloat2("Scale",     &tc->localScale.x,   0.01f, 0.f, 10.f);
					MarkDirtyIfEdited();
					RenderAnchorPicker(tc);
					ImGui::DragFloat2("Pivot",     &tc->pivot.pivot.x,  0.01f, 0.f, 1.f);
					MarkDirtyIfEdited();
					ImGui::Checkbox("Active",      &tc->active);
					MarkDirtyIfEdited();
				}
			}

			// --- UIImageComponent ---
			if (auto* ic = obj->GetComponent<UIImageComponent>())
			{
				if (ImGui::CollapsingHeader("Image", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::ColorEdit4("Color##img",      &ic->color.x);
					MarkDirtyIfEdited();
					ImGui::DragFloat("Fill Amount##img", &ic->fillAmount, 0.01f, 0.f, 1.f);
					MarkDirtyIfEdited();
					ImGui::DragFloat4("UV Rect",         &ic->uvRect.x,   0.005f, -1.f, 2.f);
					MarkDirtyIfEdited();
					ImGui::Checkbox("Flip H",            &ic->flipH);
					MarkDirtyIfEdited();
					ImGui::SameLine();
					ImGui::Checkbox("Flip V",            &ic->flipV);
					MarkDirtyIfEdited();

					ImGui::Separator();
					RenderTexturePicker("img", imageTexPathBuf_, sizeof(imageTexPathBuf_),
					    loadedTextures_, ic->texturePath, ic->texture);
				}
			}

			// --- UINineSliceComponent ---
			if (auto* ns = obj->GetComponent<UINineSliceComponent>())
			{
				if (ImGui::CollapsingHeader("Nine Slice", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::ColorEdit4("Color##ns",        &ns->color.x);
					MarkDirtyIfEdited();
					ImGui::DragFloat("Fill Amount##ns",   &ns->fillAmount, 0.01f, 0.f, 1.f);
					MarkDirtyIfEdited();
					ImGui::DragFloat("Border Left",       &ns->border.left,   0.5f, 0.f, 512.f);
					MarkDirtyIfEdited();
					ImGui::DragFloat("Border Right",      &ns->border.right,  0.5f, 0.f, 512.f);
					MarkDirtyIfEdited();
					ImGui::DragFloat("Border Top",        &ns->border.top,    0.5f, 0.f, 512.f);
					MarkDirtyIfEdited();
					ImGui::DragFloat("Border Bottom",     &ns->border.bottom, 0.5f, 0.f, 512.f);
					MarkDirtyIfEdited();

					ImGui::Separator();
					RenderTexturePicker("ns", nineSliceTexPathBuf_, sizeof(nineSliceTexPathBuf_),
					    loadedTextures_, ns->texturePath, ns->texture);
				}
			}

			// --- UICircleGaugeComponent ---
			if (auto* cg = obj->GetComponent<UICircleGaugeComponent>())
			{
				if (ImGui::CollapsingHeader("Circle Gauge", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::ColorEdit4("Color##cg",         &cg->color.x);
					MarkDirtyIfEdited();
					ImGui::DragFloat("Fill Amount##cg",    &cg->fillAmount, 0.01f, 0.f, 1.f);
					MarkDirtyIfEdited();
					ImGui::DragFloat("Start Angle (rad)",  &cg->startAngle, 0.01f, -6.28f, 6.28f);
					MarkDirtyIfEdited();
					bool cwBool = cg->clockwise > 0.f;
					if (ImGui::Checkbox("Clockwise",       &cwBool))
					{
						cg->clockwise = cwBool ? 1.f : -1.f;
						session.dirty = true;
					}

					ImGui::Separator();
					RenderTexturePicker("cg", circleGaugeTexPathBuf_, sizeof(circleGaugeTexPathBuf_),
					    loadedTextures_, cg->texturePath, cg->texture);
				}
			}

			// --- UIButtonComponent ---
			if (auto* btn = obj->GetComponent<UIButtonComponent>())
			{
				if (ImGui::CollapsingHeader("Button", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Checkbox("Interactable", &btn->interactable);
					MarkDirtyIfEdited();

					// 状態表示 (ReadOnly)
					ImGui::BeginDisabled();
					bool h = btn->isHovered, f = btn->isFocused, p = btn->isPressed;
					ImGui::Checkbox("Hovered",  &h);
					ImGui::SameLine();
					ImGui::Checkbox("Focused",  &f);
					ImGui::SameLine();
					ImGui::Checkbox("Pressed",  &p);
					ImGui::EndDisabled();
				}
			}

			// --- UITextComponent ---
			if (auto* txt = obj->GetComponent<UITextComponent>())
			{
				if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen))
				{
					// textContentBuf_ / textStyleBuf_ は DebugRender() の選択変更検出で同期する
					// --- テキスト内容 ---
					if (ImGui::InputText("Content##txt", textContentBuf_, sizeof(textContentBuf_),
					    ImGuiInputTextFlags_EnterReturnsTrue))
					{
						txt->content = textContentBuf_;
						session.dirty = true;
					}

					// --- スタイル参照 ---
					if (ImGui::InputText("Style Path##txt", textStyleBuf_, sizeof(textStyleBuf_),
					    ImGuiInputTextFlags_EnterReturnsTrue))
					{
						txt->textStylePath = textStyleBuf_;
						session.dirty = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Edit##textstyle"))
						textStyleEditor_.Open(textStyleBuf_);
					ImGui::TextDisabled("  ex) Assets/Styles/Default.textstyle.json");

					ImGui::Separator();

					// --- インスタンスオーバーライド ---
					ImGui::DragFloat("Font Size##txt", &txt->fontSize, 0.5f, 0.f, 512.f,
					                 txt->fontSize <= 0.f ? "(style)" : "%.1f px");
					MarkDirtyIfEdited();
					ImGui::DragFloat("Scale##txt",     &txt->scale,    0.01f, 0.01f, 8.f, "x%.3f");
					MarkDirtyIfEdited();
					ImGui::DragFloat2("Offset##txt",   &txt->offset.x, 0.5f, -2000.f, 2000.f, "%.1f px");
					MarkDirtyIfEdited();
					ImGui::ColorEdit4("Color##txt",    &txt->color.x);
					MarkDirtyIfEdited();
					ImGui::TextDisabled("  alpha=0 uses the Style fillColor");

					ImGui::Separator();

					// --- レイアウト ---
					ImGui::Checkbox("Word Wrap", &txt->wordWrap);
					MarkDirtyIfEdited();
					{
						static const char* ALIGN_H[] = { "Left", "Center", "Right" };
						int idx = static_cast<int>(txt->alignH);
						if (ImGui::Combo("Align H", &idx, ALIGN_H, 3))
						{
							txt->alignH = static_cast<TextAlignH>(idx);
							session.dirty = true;
						}
					}
					{
						static const char* ALIGN_V[] = { "Top", "Middle", "Bottom" };
						int idx = static_cast<int>(txt->alignV);
						if (ImGui::Combo("Align V", &idx, ALIGN_V, 3))
						{
							txt->alignV = static_cast<TextAlignV>(idx);
							session.dirty = true;
						}
					}
				}
			}

			// --- UICanvasComponent ---
			if (auto* cc = obj->GetComponent<UICanvasComponent>())
			{
				if (ImGui::CollapsingHeader("Canvas"))
				{
					ImGui::DragInt("Sort Order",    &cc->sortOrder);
					MarkDirtyIfEdited();
					ImGui::DragFloat2("Resolution", &cc->resolution.x, 1.f, 1.f, 7680.f);
					MarkDirtyIfEdited();
				}
			}
		}


		// ---- テキストオーバーレイ ------------------------------------------
		// UITextComponent の内容を ImGui DrawList でスクリーンに仮表示する。
		// フォントアトラスが未整備でも文字・位置・色を確認できる。
		void UIEditorDebugPanel::RenderTextOverlay()
		{
			if (!showTextOverlay_) return;

			UIScreen* top  = UIContext::Get().Screens().Top();
			UIObject* root = top ? top->GetRoot() : nullptr;
			if (!root) return;

			// Canvas 解像度取得
			float cW = 1920.f, cH = 1080.f;
			if (auto* canvas = root->GetComponent<UICanvasComponent>())
			{
				cW = canvas->resolution.x > 0.f ? canvas->resolution.x : cW;
				cH = canvas->resolution.y > 0.f ? canvas->resolution.y : cH;
			}

			const ImVec2 dispSz = ImGui::GetIO().DisplaySize;
			const float  scX    = dispSz.x / cW;
			const float  scY    = dispSz.y / cH;

			ImDrawList* dl = ImGui::GetBackgroundDrawList();

			// 再帰ウォーク
			struct Walker
			{
				static void Walk(UIObject* obj, ImDrawList* dl,
				                 float scX, float scY)
				{
					if (!obj || !obj->IsActiveInHierarchy()) return;

					if (auto* txt = obj->GetComponent<UITextComponent>())
					{
						if (!txt->content.empty())
						{
							auto* tc = obj->GetComponent<UITransformComponent>();
							if (tc)
							{
								const float sx = tc->worldRect.x * scX;
								const float sy = tc->worldRect.y * scY;
								const float sw = tc->worldRect.w * scX;
								const float sh = tc->worldRect.h * scY;

								// 矩形枠 (黄色)
								dl->AddRect(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh),
								            IM_COL32(255, 220, 0, 100), 0.f, 0, 1.f);

								// TextStyle から基本値を取得
								math::Vector4 fillColor = { 1.f, 1.f, 1.f, 1.f };
								float styleSize = 24.f;
								if (!txt->textStylePath.empty())
								{
									const auto& style = TextStyleCache::Get().Load(txt->textStylePath);
									fillColor = style.fillColor;
									styleSize = style.fontSize;
								}

								// フォントサイズ: (component override > style) × scale × canvas→screen
								const float baseFontSize = (txt->fontSize > 0.f) ? txt->fontSize : styleSize;
								const float fontSize     = baseFontSize * txt->scale * scX;

								// 色: color.a > 0 なら上書き、a=0 なら style の fillColor
								const auto& c = txt->color;
								const math::Vector4& fc = (c.w > 0.f) ? c : fillColor;
								const ImU32 col = IM_COL32(
									static_cast<int>(fc.x * 255),
									static_cast<int>(fc.y * 255),
									static_cast<int>(fc.z * 255),
									static_cast<int>(fc.w * 255));

								// テキストサイズ計算
								ImFont*      font = ImGui::GetFont();
								const ImVec2 tSz  = font->CalcTextSizeA(fontSize, FLT_MAX, sw, txt->content.c_str());

								// オフセット (canvas px → screen px)
								const float ox = txt->offset.x * scX;
								const float oy = txt->offset.y * scY;

								// 水平アライメント
								float tx;
								switch (txt->alignH)
								{
								case TextAlignH::Left:   tx = sx + ox;                      break;
								case TextAlignH::Right:  tx = sx + sw - tSz.x + ox;         break;
								default:                 tx = sx + (sw - tSz.x) * 0.5f + ox; break;
								}

								// 垂直アライメント
								float ty;
								switch (txt->alignV)
								{
								case TextAlignV::Top:    ty = sy + oy;                      break;
								case TextAlignV::Bottom: ty = sy + sh - tSz.y + oy;         break;
								default:                 ty = sy + (sh - tSz.y) * 0.5f + oy; break;
								}

								dl->AddText(font, fontSize, ImVec2(tx, ty), col,
								            txt->content.c_str());
							}
						}
					}

					for (UIObject* child : obj->GetChildren())
						Walk(child, dl, scX, scY);
				}
			};
			Walker::Walk(root, dl, scX, scY);
		}


		// ---- メインウィンドウ -----------------------------------------------
		void UIEditorDebugPanel::DebugRenderMenu()
		{
			ImGui::MenuItem("UI Editor", nullptr, &show_);
		}


		void UIEditorDebugPanel::DebugRender()
		{
			// オーバーレイはパネルを閉じていても動作
			RenderTextOverlay();

			if (!show_) return;

			auto& session = UIEditorSession::Get();

			// 先頭画面の screenName / documentPath を毎フレーム同期する。
			// screenName が前フレームから変わっていたら選択を捨てる。
			UIScreen* top  = UIContext::Get().Screens().Top();
			UIObject* root = top ? top->GetRoot() : nullptr;
			const std::string currentScreenName(top ? top->GetName() : std::string_view{});
			if (currentScreenName != session.screenName)
				session.ClearSelection();
			session.screenName    = currentScreenName;
			session.documentPath  = std::string(UIContext::Get().Screens().GetDocumentPath(session.screenName));

			const char* windowTitle = session.dirty
				? "UI Editor *###uieditor"
				: "UI Editor###uieditor";

			ImGui::SetNextWindowSize(ImVec2(1000, 800), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin(windowTitle))
			{
				ImGui::End();
				return;
			}

			RenderToolbar(root);
			ImGui::Separator();

			ImGui::Checkbox("Text Overlay", &showTextOverlay_);
			ImGui::SameLine();
			ImGui::TextDisabled("(UIText placeholder)");
			ImGui::Separator();

			UIObject* selectedObj = UIContext::Get().Resolve(session.selectedObject);

			// 選択が変わったとき各バッファを同期し、Animation Editor 側にも後始末させる
			if (session.selectedObject != prevSelectedHandle_)
			{
				// 旧ハンドルを解決した結果 (消えていれば null) を渡す
				UIObject* prevObj = UIContext::Get().Resolve(prevSelectedHandle_);
				prevSelectedHandle_ = session.selectedObject;
				animationEditor_.OnTargetChanged(prevObj);

				if (selectedObj)
				{
					auto n   = selectedObj->GetName();
					auto len = n.size() < sizeof(nameBuf_) - 1 ? n.size() : sizeof(nameBuf_) - 1;
					std::copy(n.begin(), n.begin() + len, nameBuf_);
					nameBuf_[len] = '\0';

					auto copyStr = [](const std::string& src, char* dst, size_t dstSz)
					{
						auto n2 = src.size() < dstSz - 1 ? src.size() : dstSz - 1;
						std::copy(src.begin(), src.begin() + n2, dst);
						dst[n2] = '\0';
					};

					if (auto* ic = selectedObj->GetComponent<UIImageComponent>())
						copyStr(ic->texturePath, imageTexPathBuf_, sizeof(imageTexPathBuf_));
					else
						imageTexPathBuf_[0] = '\0';

					if (auto* ns = selectedObj->GetComponent<UINineSliceComponent>())
						copyStr(ns->texturePath, nineSliceTexPathBuf_, sizeof(nineSliceTexPathBuf_));
					else
						nineSliceTexPathBuf_[0] = '\0';

					if (auto* cg = selectedObj->GetComponent<UICircleGaugeComponent>())
						copyStr(cg->texturePath, circleGaugeTexPathBuf_, sizeof(circleGaugeTexPathBuf_));
					else
						circleGaugeTexPathBuf_[0] = '\0';

					if (auto* txt = selectedObj->GetComponent<UITextComponent>())
					{
						copyStr(txt->content,       textContentBuf_, sizeof(textContentBuf_));
						copyStr(txt->textStylePath, textStyleBuf_,   sizeof(textStyleBuf_));
					}
					else
					{
						textContentBuf_[0] = '\0';
						textStyleBuf_[0]   = '\0';
					}
				}
				else
				{
					nameBuf_[0]                  = '\0';
					imageTexPathBuf_[0]          = '\0';
					nineSliceTexPathBuf_[0]      = '\0';
					circleGaugeTexPathBuf_[0]    = '\0';
					textContentBuf_[0]           = '\0';
					textStyleBuf_[0]             = '\0';
				}
			}

			// Animation タブを選んでいて、かつ選択オブジェクトが UIAnimationComponent を
			// 持つときだけ下部に Timeline を出す (設計書 §11 P3)
			const bool showTimeline = inspectorTab_ == InspectorTab::Animation &&
			                          selectedObj && selectedObj->GetComponent<UIAnimationComponent>();

			// Timeline は内容 (Track 本数と表示行数) から必要な高さを求める。
			// 上のツリー / プロパティが潰れないよう、残り高さの 60% で頭打ちにする
			const ImVec2  avail   = ImGui::GetContentRegionAvail();
			const float   TIMELINE_HEIGHT = std::min(animationEditor_.ComputeTimelineHeight(selectedObj), avail.y * 0.6f);
			const float   spacing = ImGui::GetStyle().ItemSpacing.y;
			// Timeline を出さないときは 0.f = 残り全部 (BeginChild の既定挙動)
			const float   paneH   = showTimeline ? (avail.y - TIMELINE_HEIGHT - spacing) : 0.f;

			// 左ペイン: オブジェクトツリー
			const float treeW = avail.x * 0.38f;
			ImGui::BeginChild("##tree", ImVec2(treeW, paneH), true);
			if (root)
				RenderTree(root);
			else
				ImGui::TextDisabled("No UIScreen");
			ImGui::EndChild();

			ImGui::SameLine();

			// 右ペイン: プロパティ
			ImGui::BeginChild("##props", ImVec2(0.f, paneH), true);
			RenderProperties(selectedObj);
			ImGui::EndChild();

			// 下部ペイン: Animation Timeline
			if (showTimeline)
			{
				ImGui::BeginChild("##timeline", ImVec2(0.f, TIMELINE_HEIGHT), true);
				animationEditor_.DrawTimelinePanel(selectedObj, ImGui::GetIO().DeltaTime);
				ImGui::EndChild();
			}

			// TextStyle 編集ポップアップ (毎フレーム呼ぶ。openRequested_ のときだけ表示される)
			textStyleEditor_.RenderPopup();

			ImGui::End();
		}

	}
}
#endif
