#include "aq.h"
#include "UIEditorDebugPanel.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include "UI/UIContext.h"
#include "UI/UIObject.h"
#include "UI/Screen/UIScreenManager.h"
#include "UI/Screen/UIScreen.h"
#include "UI/Component/UITransformComponent.h"
#include "UI/Component/UIImageComponent.h"
#include "UI/Component/UICanvasComponent.h"
#include "UI/Component/UINineSliceComponent.h"
#include "UI/Component/UICircleGaugeComponent.h"
#include "UI/Component/UIButtonComponent.h"
#include "UI/Component/UITextComponent.h"
#include "UI/Font/TextStyleCache.h"
#include "UI/Resource/UIDocumentSerializer.h"
#include "Resource/Resource.h"
#include <memory>
#include <string>
#include <cstring>
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
		} // anonymous namespace


		// ---- アンカープリセットピッカー ------------------------------------
		void UIEditorDebugPanel::RenderAnchorPicker(UITransformComponent* tc)
		{
			struct Preset { const char* label; float minX, minY, maxX, maxY; };
			static const Preset kGrid[9] = {
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
			static const Preset kStretch[3] = {
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
				const bool active = (tc->anchor.min.x == kGrid[i].minX &&
				                     tc->anchor.min.y == kGrid[i].minY &&
				                     tc->anchor.max.x == kGrid[i].maxX &&
				                     tc->anchor.max.y == kGrid[i].maxY);
				if (active)
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				if (ImGui::Button(kGrid[i].label, btnSz))
				{
					tc->anchor.min = {kGrid[i].minX, kGrid[i].minY};
					tc->anchor.max = {kGrid[i].maxX, kGrid[i].maxY};
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
				const bool active = (tc->anchor.min.x == kStretch[i].minX &&
				                     tc->anchor.min.y == kStretch[i].minY &&
				                     tc->anchor.max.x == kStretch[i].maxX &&
				                     tc->anchor.max.y == kStretch[i].maxY);
				if (active)
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				if (ImGui::Button(kStretch[i].label, {38.f, 22.f}))
				{
					tc->anchor.min = {kStretch[i].minX, kStretch[i].minY};
					tc->anchor.max = {kStretch[i].maxX, kStretch[i].maxY};
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
				UIObjectID objId,
				char* pathBuf,
				size_t pathBufSize,
				std::vector<std::shared_ptr<graphics::IShaderResourceView>>& loadedTextures,
				std::unordered_map<UIObjectID, std::string>& texturePaths,
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
						outTexture = srv;
						texturePaths[objId] = pathBuf;
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

			const bool isLeaf = node->GetChildren().empty();
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
			                        | ImGuiTreeNodeFlags_SpanAvailWidth
			                        | ImGuiTreeNodeFlags_DefaultOpen;
			if (isLeaf)   flags |= ImGuiTreeNodeFlags_Leaf;
			if (selectedHandle_ == node->GetHandle()) flags |= ImGuiTreeNodeFlags_Selected;

			const bool nameEmpty = node->GetName().empty();
			const char* label = nameEmpty ? "(no name)" : node->GetName().data();

			ImGui::PushID(node);
			const bool open = ImGui::TreeNodeEx(label, flags);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				selectedHandle_ = node->GetHandle();
			if (open)
			{
				for (UIObject* child : node->GetChildren())
					RenderTree(child);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}


		// ---- Save/Load JSON パネル ------------------------------------------
		void UIEditorDebugPanel::RenderSaveLoad(UIObject* root)
		{
			ImGui::Separator();
			ImGui::TextDisabled("--- JSON Save / Load ---");

			ImGui::SetNextItemWidth(-1.f);
			ImGui::InputText("##savepath", savePathBuf_, sizeof(savePathBuf_));
			ImGui::TextDisabled("ex) Assets/UI/screen.json");

			ImGui::BeginDisabled(root == nullptr);

			if (ImGui::Button("Save JSON", {100.f, 0.f}))
			{
				if (root && savePathBuf_[0] != '\0')
				{
					const bool ok = UIDocumentSerializer::Save(root, savePathBuf_, texturePaths_);
					std::snprintf(statusMsg_, sizeof(statusMsg_),
					    ok ? "Saved: %s" : "Save FAILED: %s", savePathBuf_);
				}
				else
				{
					std::snprintf(statusMsg_, sizeof(statusMsg_), "パスを入力してください");
				}
			}
			ImGui::EndDisabled();

			if (statusMsg_[0] != '\0')
				ImGui::TextDisabled("%s", statusMsg_);
		}


		// ---- プロパティペイン -----------------------------------------------
		void UIEditorDebugPanel::RenderProperties(UIObject* obj)
		{
			auto& ctx = UIContext::Get();

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
				}
				ImGui::SameLine();

				const bool canDelete = obj && obj->GetParent() != nullptr;
				if (!canDelete) ImGui::BeginDisabled();
				if (ImGui::Button("Delete"))
				{
					ctx.DestroyObject(selectedHandle_);
					selectedHandle_ = UIObjectHandle::Invalid();
					obj = nullptr;
				}
				if (!canDelete) ImGui::EndDisabled();

				// コンポーネント追加メニュー
				ImGui::SameLine();
				if (obj && ImGui::BeginMenu("+ Component"))
				{
					if (!obj->HasComponent<UIImageComponent>() &&
					    ImGui::MenuItem("Image"))
						obj->AddComponent<UIImageComponent>();

					if (!obj->HasComponent<UINineSliceComponent>() &&
					    ImGui::MenuItem("Nine Slice"))
						obj->AddComponent<UINineSliceComponent>();

					if (!obj->HasComponent<UICircleGaugeComponent>() &&
					    ImGui::MenuItem("Circle Gauge"))
						obj->AddComponent<UICircleGaugeComponent>();

					if (!obj->HasComponent<UIButtonComponent>() &&
					    ImGui::MenuItem("Button"))
						obj->AddComponent<UIButtonComponent>();

					if (!obj->HasComponent<UITextComponent>() &&
					    ImGui::MenuItem("Text"))
						obj->AddComponent<UITextComponent>();

					if (!obj->HasComponent<UICanvasComponent>() &&
					    ImGui::MenuItem("Canvas"))
						obj->AddComponent<UICanvasComponent>();

					ImGui::EndMenu();
				}
			}

			if (!obj)
			{
				ImGui::Separator();
				ImGui::TextDisabled("オブジェクトを選択してください");
				return;
			}

			ImGui::Separator();

			// --- 名前 ---
			if (ImGui::InputText("Name", nameBuf_, sizeof(nameBuf_),
				ImGuiInputTextFlags_EnterReturnsTrue))
				obj->SetName(nameBuf_);

			const UIObjectID objId = obj->GetHandle().id;

			// --- UITransformComponent ---
			if (auto* tc = obj->GetComponent<UITransformComponent>())
			{
				if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::DragFloat3("Position", &tc->localPosition.x, 1.f);
					ImGui::DragFloat2("Size",      &tc->sizeDelta.x,    1.f, 0.f, 4096.f);
					ImGui::DragFloat("Rotation",   &tc->rotation,       0.5f, -360.f, 360.f);
					ImGui::DragFloat2("Scale",     &tc->localScale.x,   0.01f, 0.f, 10.f);
					RenderAnchorPicker(tc);
					ImGui::DragFloat2("Pivot",     &tc->pivot.pivot.x,  0.01f, 0.f, 1.f);
					ImGui::Checkbox("Active",      &tc->active);
				}
			}

			// --- UIImageComponent ---
			if (auto* ic = obj->GetComponent<UIImageComponent>())
			{
				if (ImGui::CollapsingHeader("Image", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::ColorEdit4("Color##img",      &ic->color.x);
					ImGui::DragFloat("Fill Amount##img", &ic->fillAmount, 0.01f, 0.f, 1.f);
					ImGui::DragFloat4("UV Rect",         &ic->uvRect.x,   0.005f, -1.f, 2.f);
					ImGui::Checkbox("Flip H",            &ic->flipH);
					ImGui::SameLine();
					ImGui::Checkbox("Flip V",            &ic->flipV);

					ImGui::Separator();
					RenderTexturePicker("img", objId, texPathBuf_, sizeof(texPathBuf_),
					    loadedTextures_, texturePaths_, ic->texture);
				}
			}

			// --- UINineSliceComponent ---
			if (auto* ns = obj->GetComponent<UINineSliceComponent>())
			{
				if (ImGui::CollapsingHeader("Nine Slice", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::ColorEdit4("Color##ns",        &ns->color.x);
					ImGui::DragFloat("Fill Amount##ns",   &ns->fillAmount, 0.01f, 0.f, 1.f);
					ImGui::DragFloat("Border Left",       &ns->border.left,   0.5f, 0.f, 512.f);
					ImGui::DragFloat("Border Right",      &ns->border.right,  0.5f, 0.f, 512.f);
					ImGui::DragFloat("Border Top",        &ns->border.top,    0.5f, 0.f, 512.f);
					ImGui::DragFloat("Border Bottom",     &ns->border.bottom, 0.5f, 0.f, 512.f);

					ImGui::Separator();
					// NineSlice はテクスチャパスを独立したキーで管理 (Image と共存する場合に備え id をずらす)
					const UIObjectID nsId = objId + 0x01000000u;
					auto nsIt = texturePaths_.find(nsId);
					static char nsTexBuf[256] = {};
					if (nsIt != texturePaths_.end())
					{
						auto& p = nsIt->second;
						auto len = p.size() < sizeof(nsTexBuf) - 1 ? p.size() : sizeof(nsTexBuf) - 1;
						std::copy(p.begin(), p.begin() + len, nsTexBuf);
						nsTexBuf[len] = '\0';
					}
					RenderTexturePicker("ns", nsId, nsTexBuf, sizeof(nsTexBuf),
					    loadedTextures_, texturePaths_, ns->texture);
				}
			}

			// --- UICircleGaugeComponent ---
			if (auto* cg = obj->GetComponent<UICircleGaugeComponent>())
			{
				if (ImGui::CollapsingHeader("Circle Gauge", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::ColorEdit4("Color##cg",         &cg->color.x);
					ImGui::DragFloat("Fill Amount##cg",    &cg->fillAmount, 0.01f, 0.f, 1.f);
					ImGui::DragFloat("Start Angle (rad)",  &cg->startAngle, 0.01f, -6.28f, 6.28f);
					bool cwBool = cg->clockwise > 0.f;
					if (ImGui::Checkbox("Clockwise",       &cwBool))
						cg->clockwise = cwBool ? 1.f : -1.f;

					ImGui::Separator();
					const UIObjectID cgId = objId + 0x02000000u;
					auto cgIt = texturePaths_.find(cgId);
					static char cgTexBuf[256] = {};
					if (cgIt != texturePaths_.end())
					{
						auto& p = cgIt->second;
						auto len = p.size() < sizeof(cgTexBuf) - 1 ? p.size() : sizeof(cgTexBuf) - 1;
						std::copy(p.begin(), p.begin() + len, cgTexBuf);
						cgTexBuf[len] = '\0';
					}
					RenderTexturePicker("cg", cgId, cgTexBuf, sizeof(cgTexBuf),
					    loadedTextures_, texturePaths_, cg->texture);
				}
			}

			// --- UIButtonComponent ---
			if (auto* btn = obj->GetComponent<UIButtonComponent>())
			{
				if (ImGui::CollapsingHeader("Button", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Checkbox("Interactable", &btn->interactable);

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
					static char contentBuf[512] = {};
					static char styleBuf[512]   = {};

					// 選択が変わった時だけバッファ同期 (毎フレーム上書き防止)
					if (selectedHandle_ != prevSelectedHandle_)
					{
						auto copyTo = [](const std::string& src, char* dst, size_t dstSz)
						{
							auto n = src.size() < dstSz - 1 ? src.size() : dstSz - 1;
							std::copy(src.begin(), src.begin() + n, dst);
							dst[n] = '\0';
						};
						copyTo(txt->content,       contentBuf, sizeof(contentBuf));
						copyTo(txt->textStylePath, styleBuf,   sizeof(styleBuf));
					}

					// --- テキスト内容 ---
					if (ImGui::InputText("Content##txt", contentBuf, sizeof(contentBuf),
					    ImGuiInputTextFlags_EnterReturnsTrue))
						txt->content = contentBuf;

					// --- スタイル参照 ---
					if (ImGui::InputText("Style Path##txt", styleBuf, sizeof(styleBuf),
					    ImGuiInputTextFlags_EnterReturnsTrue))
						txt->textStylePath = styleBuf;
					ImGui::TextDisabled("  ex) Assets/Styles/Default.textstyle.json");

					ImGui::Separator();

					// --- インスタンスオーバーライド ---
					ImGui::DragFloat("Font Size##txt", &txt->fontSize, 0.5f, 0.f, 512.f,
					                 txt->fontSize <= 0.f ? "(style)" : "%.1f px");
					ImGui::DragFloat("Scale##txt",     &txt->scale,    0.01f, 0.01f, 8.f, "x%.3f");
					ImGui::DragFloat2("Offset##txt",   &txt->offset.x, 0.5f, -2000.f, 2000.f, "%.1f px");
					ImGui::ColorEdit4("Color##txt",    &txt->color.x);
					ImGui::TextDisabled("  alpha=0 のとき Style の fillColor を使用");

					ImGui::Separator();

					// --- レイアウト ---
					ImGui::Checkbox("Word Wrap", &txt->wordWrap);
					{
						static const char* kAlignH[] = { "Left", "Center", "Right" };
						int idx = static_cast<int>(txt->alignH);
						if (ImGui::Combo("Align H", &idx, kAlignH, 3))
							txt->alignH = static_cast<TextAlignH>(idx);
					}
					{
						static const char* kAlignV[] = { "Top", "Middle", "Bottom" };
						int idx = static_cast<int>(txt->alignV);
						if (ImGui::Combo("Align V", &idx, kAlignV, 3))
							txt->alignV = static_cast<TextAlignV>(idx);
					}
				}
			}

			// --- UICanvasComponent ---
			if (auto* cc = obj->GetComponent<UICanvasComponent>())
			{
				if (ImGui::CollapsingHeader("Canvas"))
				{
					ImGui::DragInt("Sort Order",    &cc->sortOrder);
					ImGui::DragFloat2("Resolution", &cc->resolution.x, 1.f, 1.f, 7680.f);
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

			ImGui::SetNextWindowSize(ImVec2(700, 560), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("UI Editor"))
			{
				ImGui::End();
				return;
			}

			ImGui::Checkbox("Text Overlay", &showTextOverlay_);
			ImGui::SameLine();
			ImGui::TextDisabled("(UIText 仮表示)");
			ImGui::Separator();

			UIScreen* top  = UIContext::Get().Screens().Top();
			UIObject* root = top ? top->GetRoot() : nullptr;

			UIObject* selectedObj = UIContext::Get().Resolve(selectedHandle_);

			// 選択が変わったとき各バッファを同期
			if (selectedHandle_ != prevSelectedHandle_)
			{
				prevSelectedHandle_ = selectedHandle_;
				if (selectedObj)
				{
					auto n   = selectedObj->GetName();
					auto len = n.size() < sizeof(nameBuf_) - 1 ? n.size() : sizeof(nameBuf_) - 1;
					std::copy(n.begin(), n.begin() + len, nameBuf_);
					nameBuf_[len] = '\0';

					auto it = texturePaths_.find(selectedHandle_.id);
					if (it != texturePaths_.end())
					{
						auto& p   = it->second;
						auto plen = p.size() < sizeof(texPathBuf_) - 1 ? p.size() : sizeof(texPathBuf_) - 1;
						std::copy(p.begin(), p.begin() + plen, texPathBuf_);
						texPathBuf_[plen] = '\0';
					}
					else
					{
						texPathBuf_[0] = '\0';
					}
				}
				else
				{
					nameBuf_[0]    = '\0';
					texPathBuf_[0] = '\0';
				}
			}

			// 左ペイン: オブジェクトツリー + Save/Load
			const float treeW = ImGui::GetContentRegionAvail().x * 0.38f;
			ImGui::BeginChild("##tree", ImVec2(treeW, 0.f), true);
			if (root)
				RenderTree(root);
			else
				ImGui::TextDisabled("UIScreen がありません");

			RenderSaveLoad(root);
			ImGui::EndChild();

			ImGui::SameLine();

			// 右ペイン: プロパティ
			ImGui::BeginChild("##props", ImVec2(0.f, 0.f), true);
			RenderProperties(selectedObj);
			ImGui::EndChild();

			ImGui::End();
		}

	}
}
#endif
