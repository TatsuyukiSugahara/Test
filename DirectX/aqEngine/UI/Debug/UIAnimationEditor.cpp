#include "aq.h"
#include "UIAnimationEditor.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include "UI/UIContext.h"
#include "UI/UIObject.h"
#include "UI/Screen/UIScreenManager.h"
#include "UI/Screen/UIScreen.h"
#include "UI/Component/UIAnimationComponent.h"
#include "UI/Animation/UIAnimationSerializer.h"
#include "UI/Animation/UIAnimationClip.h"
#include "UI/Animation/UIClipTrack.h"
#include "UI/Animation/UIAnimationTrack.h"
#include "Util/SimpleJson.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace aq
{
	namespace ui
	{
		static constexpr float kRowH         = 22.f;
		static constexpr float kLabelW       = 170.f;
		static constexpr float kRulerH       = 24.f;
		static constexpr float kCtHeaderH    = 26.f;
		static constexpr float kDiamondR     = 5.f;
		static constexpr float kMinZoom      = 40.f;
		static constexpr float kMaxZoom      = 800.f;


		// ---- メニュー登録 -------------------------------------------------------

		void UIAnimationEditor::DebugRenderMenu()
		{
			if (ImGui::BeginMenu("UI"))
			{
				ImGui::MenuItem("Animation Editor", nullptr, &show_);
				ImGui::EndMenu();
			}
		}


		// ---- メインウィンドウ ---------------------------------------------------

		void UIAnimationEditor::DebugRender()
		{
			if (!show_) return;

			const ImGuiWindowFlags winFlags = windowPinned_
				? (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)
				: ImGuiWindowFlags_None;

			ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("UI Animation Editor", &show_, winFlags))
			{
				ImGui::End();
				return;
			}

			// dt 取得 (preview 用)
			const float dt = ImGui::GetIO().DeltaTime;

			if (ImGui::SmallButton(windowPinned_ ? "[固定中] 解除" : "[固定]"))
				windowPinned_ = !windowPinned_;
			ImGui::SameLine();

			DrawObjectPicker();
			ImGui::Separator();

			UIObject* target = UIContext::Get().Resolve(targetHandle_);
			if (!target)
			{
				ImGui::TextDisabled("対象 UIObject を選択してください");
				ImGui::End();
				return;
			}

			auto* anim = target->GetComponent<UIAnimationComponent>();
			if (!anim)
			{
				ImGui::TextColored(ImVec4(1,0.6f,0,1), "UIAnimationComponent がありません");
				if (ImGui::Button("コンポーネント追加"))
					anim = target->AddComponent<UIAnimationComponent>();
				if (!anim) { ImGui::End(); return; }
			}

			// selectedClip_ の存在確認
			if (!selectedClip_.empty() && anim->clips.find(selectedClip_) == anim->clips.end())
				selectedClip_.clear();

			// 左右分割
			const float leftW  = 260.f;
			const float totalH = ImGui::GetContentRegionAvail().y - 120.f; // 下部パネル分を確保

			ImGui::BeginChild("##left", ImVec2(leftW, totalH), true);
			DrawClipPanel(target);
			ImGui::EndChild();

			ImGui::SameLine();

			ImGui::BeginChild("##right", ImVec2(0, totalH), true);
			if (!selectedClip_.empty())
			{
				auto& clip = anim->clips[selectedClip_];
				DrawTimeline(target, clip);
			}
			else
			{
				ImGui::TextDisabled("クリップを選択してください");
			}
			ImGui::EndChild();

			ImGui::Separator();

			// 下部: キーフレーム Inspector + Preview Controls + Save/Load
			ImGui::BeginChild("##bottom", ImVec2(0, 0));
			{
				if (!selectedClip_.empty())
				{
					auto& clip = anim->clips[selectedClip_];

					// キーフレーム Inspector (左半分)
					ImGui::BeginChild("##kfinspector", ImVec2(300, 0), false);
					DrawKeyframeInspector(clip);
					ImGui::EndChild();

					ImGui::SameLine();

					// Preview Controls (中央)
					ImGui::BeginChild("##preview", ImVec2(320, 0), false);
					DrawPreviewControls(target, clip, dt);
					ImGui::EndChild();

					ImGui::SameLine();
				}

				// Save / Load (右)
				ImGui::BeginChild("##saveload", ImVec2(0, 0), false);
				DrawSaveLoad(target);
				ImGui::EndChild();
			}
			ImGui::EndChild();

			ImGui::End();
		}


		// ---- Object Picker --------------------------------------------------

		void UIAnimationEditor::DrawObjectPicker()
		{
			UIObject* cur = UIContext::Get().Resolve(targetHandle_);
			const char* label = cur ? cur->GetName().data() : "(未選択)";

			ImGui::Text("対象: ");
			ImGui::SameLine();
			if (ImGui::Button(label))
			{
				objPickerOpen_ = true;
				objList_.clear();
				objFilterBuf_[0] = '\0';

				// 現在の全スクリーンから UIObject を列挙
				auto& screens = UIContext::Get().Screens();
				for (int si = 0; si < screens.StackSize(); ++si)
				{
					UIScreen* s = screens.GetScreen(si);
					if (s && s->GetRoot())
						CollectObjects(s->GetRoot(), 0);
				}
			}

			if (objPickerOpen_)
			{
				ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_Always);
				if (ImGui::Begin("Object Picker", &objPickerOpen_))
				{
					ImGui::InputText("Filter", objFilterBuf_, sizeof(objFilterBuf_));
					ImGui::Separator();

					std::string filterStr(objFilterBuf_);
					for (auto& e : objList_)
					{
						if (!filterStr.empty())
						{
							if (e.displayName.find(filterStr) == std::string::npos)
								continue;
						}
						bool sel = (e.handle == targetHandle_);
						if (ImGui::Selectable(e.displayName.c_str(), sel))
						{
							targetHandle_    = e.handle;
							selectedClip_.clear();
							selClipTrackIdx_ = -1;
							selPropTrackIdx_ = -1;
							selKeyframeIdx_  = -1;
							if (hasSnapshot_)
							{
								if (auto* obj = UIContext::Get().Resolve(targetHandle_))
									RestoreSnapshot(obj);
								hasSnapshot_ = false;
							}
							objPickerOpen_ = false;
						}
					}
				}
				ImGui::End();
			}
		}

		void UIAnimationEditor::CollectObjects(UIObject* obj, int depth)
		{
			if (!obj) return;
			std::string indent(depth * 2, ' ');
			ObjEntry e;
			e.handle      = obj->GetHandle();
			e.displayName = indent + std::string(obj->GetName());
			objList_.push_back(e);
			for (UIObject* child : obj->GetChildren())
				CollectObjects(child, depth + 1);
		}


		// ---- Left Panel: Clip + Track Tree ----------------------------------

		void UIAnimationEditor::DrawClipPanel(UIObject* obj)
		{
			auto* anim = obj->GetComponent<UIAnimationComponent>();
			if (!anim) return;

			// ---- Clip selector ----
			ImGui::Text("Clips");
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Clip"))
			{
				UIAnimationClip nc;
				nc.name     = "NewClip";
				nc.duration = 1.f;
				// 名前重複回避
				int idx = 1;
				while (anim->clips.count(nc.name))
					nc.name = "NewClip" + std::to_string(idx++);
				selectedClip_ = nc.name;
				anim->clips[nc.name] = std::move(nc);
				selClipTrackIdx_ = selPropTrackIdx_ = selKeyframeIdx_ = -1;
			}

			ImGui::Separator();

			for (auto& [name, clip] : anim->clips)
			{
				bool sel = (name == selectedClip_);
				if (ImGui::Selectable(name.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick))
				{
					if (selectedClip_ != name)
					{
						selectedClip_    = name;
						selClipTrackIdx_ = selPropTrackIdx_ = selKeyframeIdx_ = -1;
						scrubTime_       = 0.f;
						isPlaying_       = false;
						if (hasSnapshot_) { RestoreSnapshot(obj); hasSnapshot_ = false; }
					}
				}
			}

			if (selectedClip_.empty() || !anim->clips.count(selectedClip_))
				return;

			auto& clip = anim->clips[selectedClip_];

			ImGui::Separator();
			ImGui::Text("Clip: %s", clip.name.c_str());

			// Clip 名変更
			strncpy_s(clipNameBuf_, sizeof(clipNameBuf_), clip.name.c_str(), _TRUNCATE);
			ImGui::SetNextItemWidth(120.f);
			if (ImGui::InputText("Name##clip", clipNameBuf_, sizeof(clipNameBuf_),
			                     ImGuiInputTextFlags_EnterReturnsTrue))
			{
				std::string newName(clipNameBuf_);
				if (!newName.empty() && !anim->clips.count(newName))
				{
					auto node = anim->clips.extract(selectedClip_);
					node.key() = newName;
					node.mapped().name = newName;
					anim->clips.insert(std::move(node));
					selectedClip_ = newName;
				}
			}

			ImGui::SetNextItemWidth(80.f);
			ImGui::DragFloat("Duration", &clip.duration, 0.01f, 0.01f, 60.f, "%.2fs");
			clip.duration = std::max(clip.duration, 0.01f);

			if (ImGui::SmallButton("- Clip"))
			{
				if (hasSnapshot_) { RestoreSnapshot(obj); hasSnapshot_ = false; }
				anim->clips.erase(selectedClip_);
				selectedClip_.clear();
				selClipTrackIdx_ = selPropTrackIdx_ = selKeyframeIdx_ = -1;
				return;
			}

			ImGui::Separator();
			DrawClipTrackTree(clip);
		}

		void UIAnimationEditor::DrawClipTrackTree(UIAnimationClip& clip)
		{
			ImGui::Text("Tracks");
			ImGui::SameLine();
			if (ImGui::SmallButton("+ ClipTrack"))
			{
				UIClipTrack ct;
				ct.name     = "Track";
				ct.loopFrom = -1.f;
				clip.clipTracks.push_back(std::move(ct));
				selClipTrackIdx_ = static_cast<int>(clip.clipTracks.size()) - 1;
				selPropTrackIdx_ = selKeyframeIdx_ = -1;
			}

			static const char* kCondLabels[] = { "Default", "Bool", "Trigger" };

			for (int ci = 0; ci < (int)clip.clipTracks.size(); ++ci)
			{
				auto& ct = clip.clipTracks[ci];
				ImGui::PushID(ci);

				bool ctSel = (ci == selClipTrackIdx_);
				char ctLabel[128];
				std::snprintf(ctLabel, sizeof(ctLabel), "[%s] %s",
				              UIAnimationSerializer::ConditionToStr(ct.condition),
				              ct.name.empty() ? "(no name)" : ct.name.c_str());

				bool open = ImGui::TreeNodeEx(ctLabel,
				    ImGuiTreeNodeFlags_DefaultOpen |
				    (ctSel ? ImGuiTreeNodeFlags_Selected : 0));

				if (ImGui::IsItemClicked())
				{
					selClipTrackIdx_ = ci;
					selPropTrackIdx_ = selKeyframeIdx_ = -1;
				}

				// ClipTrack 設定 (collapsed 時も表示)
				if (open)
				{
					ImGui::SetNextItemWidth(100.f);
					strncpy_s(ctNameBuf_, sizeof(ctNameBuf_), ct.name.c_str(), _TRUNCATE);
					if (ImGui::InputText("Name##ct", ctNameBuf_, sizeof(ctNameBuf_),
					                     ImGuiInputTextFlags_EnterReturnsTrue))
						ct.name = ctNameBuf_;

					int condIdx = static_cast<int>(ct.condition);
					ImGui::SetNextItemWidth(80.f);
					if (ImGui::Combo("Cond", &condIdx, kCondLabels, 3))
						ct.condition = static_cast<UITrackCondition>(condIdx);

					if (ct.condition != UITrackCondition::Default)
					{
						strncpy_s(condParamBuf_, sizeof(condParamBuf_), ct.conditionParam.c_str(), _TRUNCATE);
						ImGui::SetNextItemWidth(100.f);
						if (ImGui::InputText("Param##ct", condParamBuf_, sizeof(condParamBuf_),
						                     ImGuiInputTextFlags_EnterReturnsTrue))
							ct.conditionParam = condParamBuf_;
					}

					ImGui::Checkbox("Restore", &ct.restoreOnComplete);
					ImGui::SameLine();
					bool loop = (ct.loopFrom >= 0.f);
					if (ImGui::Checkbox("Loop", &loop))
						ct.loopFrom = loop ? 0.f : -1.f;
					if (loop)
					{
						ImGui::SetNextItemWidth(60.f);
						ImGui::DragFloat("LoopFrom", &ct.loopFrom, 0.01f, 0.f, 60.f, "%.2f");
						ImGui::SameLine();
						ImGui::Checkbox("SkipFirst", &ct.loopSkipFirst);
					}

					// PropTrack リスト
					static const char* kPropLabels[] = {
						"PositionX","PositionY","PositionZ",
						"ScaleX","ScaleY","Rotation",
						"SizeDeltaX","SizeDeltaY",
						"ColorR","ColorG","ColorB","ColorA",
						"FillAmount","Active",
						"NineSliceBorderLeft","NineSliceBorderRight",
						"NineSliceBorderTop","NineSliceBorderBottom",
						"TextCharCount",
					};
					static const UIAnimatedProperty kPropValues[] = {
						UIAnimatedProperty::PositionX, UIAnimatedProperty::PositionY,
						UIAnimatedProperty::PositionZ, UIAnimatedProperty::ScaleX,
						UIAnimatedProperty::ScaleY,    UIAnimatedProperty::Rotation,
						UIAnimatedProperty::SizeDeltaX,UIAnimatedProperty::SizeDeltaY,
						UIAnimatedProperty::ColorR,    UIAnimatedProperty::ColorG,
						UIAnimatedProperty::ColorB,    UIAnimatedProperty::ColorA,
						UIAnimatedProperty::FillAmount,UIAnimatedProperty::Active,
						UIAnimatedProperty::NineSliceBorderLeft,
						UIAnimatedProperty::NineSliceBorderRight,
						UIAnimatedProperty::NineSliceBorderTop,
						UIAnimatedProperty::NineSliceBorderBottom,
						UIAnimatedProperty::TextCharCount,
					};
					constexpr int kPropCount = 19;

					if (ImGui::SmallButton("+ PropTrack"))
					{
						UIAnimationTrack pt;
						pt.property = UIAnimatedProperty::PositionX;
						ct.tracks.push_back(std::move(pt));
						selClipTrackIdx_ = ci;
						selPropTrackIdx_ = static_cast<int>(ct.tracks.size()) - 1;
						selKeyframeIdx_  = -1;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("- ClipTrack"))
					{
						clip.clipTracks.erase(clip.clipTracks.begin() + ci);
						if (selClipTrackIdx_ >= (int)clip.clipTracks.size())
							selClipTrackIdx_ = (int)clip.clipTracks.size() - 1;
						selPropTrackIdx_ = selKeyframeIdx_ = -1;
						ImGui::TreePop();
						ImGui::PopID();
						return;
					}

					for (int pi = 0; pi < (int)ct.tracks.size(); ++pi)
					{
						auto& pt = ct.tracks[pi];
						ImGui::PushID(pi);

						bool ptSel = (ci == selClipTrackIdx_ && pi == selPropTrackIdx_);
						char ptLabel[64];
						std::snprintf(ptLabel, sizeof(ptLabel), "  %s",
						              UIAnimationSerializer::PropertyToStr(pt.property));

						if (ImGui::Selectable(ptLabel, ptSel, ImGuiSelectableFlags_AllowOverlap))
						{
							selClipTrackIdx_ = ci;
							selPropTrackIdx_ = pi;
							selKeyframeIdx_  = -1;
						}
						ImGui::SameLine(120.f);

						// Property ドロップダウン
						int propIdx = 0;
						for (int k = 0; k < kPropCount; ++k)
						{
							if (kPropValues[k] == pt.property) { propIdx = k; break; }
						}
						ImGui::SetNextItemWidth(140.f);
						if (ImGui::Combo("##prop", &propIdx, kPropLabels, kPropCount))
							pt.property = kPropValues[propIdx];

						ImGui::SameLine();
						if (ImGui::SmallButton("-##pt"))
						{
							ct.tracks.erase(ct.tracks.begin() + pi);
							if (selPropTrackIdx_ >= (int)ct.tracks.size())
								selPropTrackIdx_ = (int)ct.tracks.size() - 1;
							selKeyframeIdx_ = -1;
							ImGui::PopID();
							continue;
						}

						ImGui::PopID();
					}
					ImGui::TreePop();
				}

				ImGui::PopID();
			}
		}


		// ---- Timeline -------------------------------------------------------

		void UIAnimationEditor::DrawTimeline(UIObject* obj, UIAnimationClip& clip)
		{
			const float totalDur = clip.duration;
			const float totalW   = totalDur * zoomPxPerSec_;

			// 全行数カウント (ClipTrack ヘッダ + PropTrack 行)
			int totalRows = 0;
			for (auto& ct : clip.clipTracks)
				totalRows += 1 + (int)ct.tracks.size();

			const float contentW = kLabelW + totalW + 20.f;
			const float contentH = kRulerH + totalRows * kRowH + 10.f;

			// 操作ヒント
			ImGui::TextDisabled("右クリック: キー追加/削除  |  Ctrl+Click: キー追加  |  Delete: 削除  |  Drag: 移動  |  Wheel: ズーム");

			// 横スクロール可能な子ウィンドウ
			ImGui::BeginChild("##tlscroll", ImVec2(0, 0), false,
			                  ImGuiWindowFlags_HorizontalScrollbar);

			// マウスホイールでズーム (window hovered 時のみ)
			if (ImGui::IsWindowHovered())
			{
				float wheel = ImGui::GetIO().MouseWheel;
				if (wheel != 0.f)
					zoomPxPerSec_ = std::clamp(zoomPxPerSec_ * (1.f + wheel * 0.1f),
					                            kMinZoom, kMaxZoom);
			}

			ImDrawList* dl  = ImGui::GetWindowDrawList();
			const ImVec2 wp = ImGui::GetWindowPos();
			const float  sx = ImGui::GetScrollX();
			const float  sy = ImGui::GetScrollY();

			// content 座標 -> screen 座標: screen_x = wp.x + content_x - sx
			// ラベル列: content x = 0 .. kLabelW (スクロール時は画面外へ)
			// タイムライン: content x = kLabelW ..
			const float tlX = wp.x + kLabelW - sx;  // タイムライン開始 screen x
			const float tlY = wp.y - sy;             // 先頭 screen y

			// 全体背景
			dl->AddRectFilled(ImVec2(wp.x, wp.y),
			                  ImVec2(wp.x + ImGui::GetWindowWidth(),
			                         wp.y + ImGui::GetWindowHeight()),
			                  IM_COL32(22, 22, 30, 255));

			// ルーラー描画
			DrawRuler(dl, tlX, tlY, totalW, totalDur);

			// トラック行描画
			float rowY = tlY + kRulerH;
			for (int ci = 0; ci < (int)clip.clipTracks.size(); ++ci)
			{
				auto& ct = clip.clipTracks[ci];

				// ClipTrack ヘッダ行
				float hdrLeft  = wp.x - sx;          // content x = 0
				float hdrRight = wp.x - sx + contentW;
				dl->AddRectFilled(ImVec2(hdrLeft, rowY), ImVec2(hdrRight, rowY + kCtHeaderH),
				                  IM_COL32(60, 60, 80, 220));
				dl->AddLine(ImVec2(hdrLeft, rowY + kCtHeaderH - 1),
				            ImVec2(hdrRight, rowY + kCtHeaderH - 1),
				            IM_COL32(100, 100, 140, 200));

				char ctLabel[128];
				std::snprintf(ctLabel, sizeof(ctLabel), "[%s] %s",
				              UIAnimationSerializer::ConditionToStr(ct.condition), ct.name.c_str());
				// ラベルは固定位置 (wp.x) でクリップ
				dl->AddText(ImVec2(wp.x + 4.f, rowY + 4.f), IM_COL32(200, 210, 230, 255), ctLabel);
				rowY += kCtHeaderH;

				// PropTrack 行
				for (int pi = 0; pi < (int)ct.tracks.size(); ++pi)
				{
					DrawTrackRow(dl, clip, ci, pi, tlX, rowY, kRowH, totalDur);
					rowY += kRowH;
				}
			}

			// スクラバー縦線 + 三角ヘッド
			const float scrubX = tlX + scrubTime_ * zoomPxPerSec_;
			dl->AddLine(ImVec2(scrubX, tlY + kRulerH),
			            ImVec2(scrubX, tlY + contentH),
			            IM_COL32(255, 200, 50, 220), 2.f);
			dl->AddTriangleFilled(
				ImVec2(scrubX - 6.f, tlY),
				ImVec2(scrubX + 6.f, tlY),
				ImVec2(scrubX,       tlY + 12.f),
				IM_COL32(255, 200, 50, 255));

			// Dummy でスクロール範囲を確定させる (描画後に置く)
			ImGui::Dummy(ImVec2(contentW, contentH));

			// ルーラー範囲でクリック / ドラッグ → スクラブ
			const ImVec2 mouse = ImGui::GetMousePos();
			const bool inRuler = mouse.x >= tlX && mouse.x <= tlX + totalW &&
			                     mouse.y >= tlY && mouse.y <= tlY + kRulerH;

			if (inRuler && ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
			{
				float t = (mouse.x - tlX) / zoomPxPerSec_;
				scrubTime_ = std::clamp(t, 0.f, totalDur);
				if (!hasSnapshot_) { TakeSnapshot(obj, clip); hasSnapshot_ = true; }
				ApplyScrub(obj, clip);
			}

			// Ctrl+クリックで選択 PropTrack にキーフレーム追加
			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) &&
			    ImGui::GetIO().KeyCtrl &&
			    selClipTrackIdx_ >= 0 && selPropTrackIdx_ >= 0)
			{
				float t = std::clamp((mouse.x - tlX) / zoomPxPerSec_, 0.f, totalDur);
				if (selClipTrackIdx_ < (int)clip.clipTracks.size())
				{
					auto& ct = clip.clipTracks[selClipTrackIdx_];
					if (selPropTrackIdx_ < (int)ct.tracks.size())
					{
						auto& pt = ct.tracks[selPropTrackIdx_];
						UIKeyframe kf{ t, pt.ReadFrom(obj), EaseType::Linear };
						pt.keyframes.push_back(kf);
						std::sort(pt.keyframes.begin(), pt.keyframes.end(),
						          [](const UIKeyframe& a, const UIKeyframe& b)
						          { return a.time < b.time; });
					}
				}
			}

			// キーフレームドラッグ処理 (クリック開始位置がキーフレーム上のときのみ動く)
			if (dragKfCtIdx_ >= 0 && dragKfPtIdx_ >= 0 && dragKfKiIdx_ >= 0 &&
			    dragKfCtIdx_ < (int)clip.clipTracks.size())
			{
				auto& dct = clip.clipTracks[dragKfCtIdx_];
				if (dragKfPtIdx_ < (int)dct.tracks.size())
				{
					auto& dpt = dct.tracks[dragKfPtIdx_];
					if (dragKfKiIdx_ < (int)dpt.keyframes.size())
					{
						auto& dkf = dpt.keyframes[dragKfKiIdx_];
						if (ImGui::IsMouseDragging(0, 2.f))
						{
							isDraggingKf_ = true;
							const float dx = ImGui::GetIO().MouseDelta.x;
							dkf.time = std::clamp(dkf.time + dx / zoomPxPerSec_, 0.f, totalDur);
						}
						if (isDraggingKf_ && ImGui::IsMouseReleased(0))
						{
							const float savedTime = dkf.time;
							std::sort(dpt.keyframes.begin(), dpt.keyframes.end(),
							          [](const UIKeyframe& a, const UIKeyframe& b)
							          { return a.time < b.time; });
							for (int ni = 0; ni < (int)dpt.keyframes.size(); ++ni)
							{
								if (std::abs(dpt.keyframes[ni].time - savedTime) < 1e-5f)
								{ selKeyframeIdx_ = ni; break; }
							}
						}
					}
				}
			}
			if (ImGui::IsMouseReleased(0))
			{
				isDraggingKf_ = false;
				dragKfCtIdx_ = dragKfPtIdx_ = dragKfKiIdx_ = -1;
			}

			// Deleteキー → 選択キーフレーム削除
			if (ImGui::IsWindowHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
			    selClipTrackIdx_ >= 0 && selPropTrackIdx_ >= 0 && selKeyframeIdx_ >= 0 &&
			    selClipTrackIdx_ < (int)clip.clipTracks.size())
			{
				auto& ct = clip.clipTracks[selClipTrackIdx_];
				if (selPropTrackIdx_ < (int)ct.tracks.size())
				{
					auto& pt = ct.tracks[selPropTrackIdx_];
					if (selKeyframeIdx_ < (int)pt.keyframes.size())
					{
						pt.keyframes.erase(pt.keyframes.begin() + selKeyframeIdx_);
						selKeyframeIdx_ = -1;
					}
				}
			}

			// キーフレームコンテキストメニュー
			if (ImGui::BeginPopup("kf_ctx"))
			{
				if (selClipTrackIdx_ >= 0 && selPropTrackIdx_ >= 0 && selKeyframeIdx_ >= 0 &&
				    selClipTrackIdx_ < (int)clip.clipTracks.size())
				{
					auto& ct = clip.clipTracks[selClipTrackIdx_];
					if (selPropTrackIdx_ < (int)ct.tracks.size())
					{
						auto& pt = ct.tracks[selPropTrackIdx_];
						if (selKeyframeIdx_ < (int)pt.keyframes.size())
						{
							auto& kf = pt.keyframes[selKeyframeIdx_];
							ImGui::TextDisabled("t=%.3f  v=%.3f", kf.time, kf.value);
							ImGui::Separator();
							static const char* kEaseMenuLabels[] =
								{ "Linear","EaseIn","EaseOut","EaseInOut","Bezier" };
							if (ImGui::BeginMenu("Ease"))
							{
								for (int ei = 0; ei < 5; ++ei)
								{
									bool isCur = (static_cast<int>(kf.ease) == ei);
									if (ImGui::MenuItem(kEaseMenuLabels[ei], nullptr, isCur))
										kf.ease = static_cast<EaseType>(ei);
								}
								ImGui::EndMenu();
							}
							ImGui::Separator();
							if (ImGui::MenuItem("Duplicate Key"))
							{
								UIKeyframe dup = kf;
								dup.time = std::min(dup.time + 0.1f, clip.duration);
								pt.keyframes.push_back(dup);
								std::sort(pt.keyframes.begin(), pt.keyframes.end(),
								          [](const UIKeyframe& a, const UIKeyframe& b)
								          { return a.time < b.time; });
								for (int ni = 0; ni < (int)pt.keyframes.size(); ++ni)
								{
									if (std::abs(pt.keyframes[ni].time - dup.time) < 1e-5f)
									{ selKeyframeIdx_ = ni; break; }
								}
							}
							ImGui::Separator();
							if (ImGui::MenuItem("Delete Key"))
							{
								pt.keyframes.erase(pt.keyframes.begin() + selKeyframeIdx_);
								selKeyframeIdx_ = -1;
							}
						}
					}
				}
				ImGui::EndPopup();
			}

			// トラック空領域コンテキストメニュー
			if (ImGui::BeginPopup("track_ctx"))
			{
				if (selClipTrackIdx_ >= 0 && selPropTrackIdx_ >= 0 &&
				    selClipTrackIdx_ < (int)clip.clipTracks.size())
				{
					auto& ct = clip.clipTracks[selClipTrackIdx_];
					if (selPropTrackIdx_ < (int)ct.tracks.size())
					{
						auto& pt = ct.tracks[selPropTrackIdx_];
						ImGui::TextDisabled("%s  t=%.3f",
						    UIAnimationSerializer::PropertyToStr(pt.property), ctxClickTime_);
						ImGui::Separator();
						if (ImGui::MenuItem("Add Key here"))
						{
							UIKeyframe kf{ ctxClickTime_, pt.Sample(ctxClickTime_), EaseType::Linear };
							pt.keyframes.push_back(kf);
							std::sort(pt.keyframes.begin(), pt.keyframes.end(),
							          [](const UIKeyframe& a, const UIKeyframe& b)
							          { return a.time < b.time; });
							for (int ni = 0; ni < (int)pt.keyframes.size(); ++ni)
							{
								if (std::abs(pt.keyframes[ni].time - ctxClickTime_) < 1e-5f)
								{ selKeyframeIdx_ = ni; break; }
							}
						}
						if (ImGui::MenuItem("Add Key at Scrub"))
						{
							UIKeyframe kf{ scrubTime_, pt.Sample(scrubTime_), EaseType::Linear };
							pt.keyframes.push_back(kf);
							std::sort(pt.keyframes.begin(), pt.keyframes.end(),
							          [](const UIKeyframe& a, const UIKeyframe& b)
							          { return a.time < b.time; });
							for (int ni = 0; ni < (int)pt.keyframes.size(); ++ni)
							{
								if (std::abs(pt.keyframes[ni].time - scrubTime_) < 1e-5f)
								{ selKeyframeIdx_ = ni; break; }
							}
						}
					}
				}
				ImGui::EndPopup();
			}

			ImGui::EndChild();
		}

		void UIAnimationEditor::DrawRuler(
			ImDrawList* dl, float ox, float oy, float w, float duration)
		{
			dl->AddRectFilled(ImVec2(ox, oy),
			                  ImVec2(ox + w, oy + kRulerH),
			                  IM_COL32(40, 40, 50, 255));

			// 目盛り間隔: ズームに応じて 0.1 / 0.5 / 1.0 / 2.0 s 単位で切り替え
			float step = 0.1f;
			if (zoomPxPerSec_ < 80.f)  step = 1.f;
			else if (zoomPxPerSec_ < 200.f) step = 0.5f;

			char buf[16];
			for (float t = 0.f; t <= duration + 1e-4f; t += step)
			{
				float x = ox + t * zoomPxPerSec_;
				bool  major = (std::fmod(t + 1e-5f, 1.f) < step * 0.5f);
				float tickH = major ? kRulerH * 0.6f : kRulerH * 0.3f;
				dl->AddLine(ImVec2(x, oy + kRulerH - tickH),
				            ImVec2(x, oy + kRulerH),
				            IM_COL32(180, 180, 180, 200));
				if (major)
				{
					std::snprintf(buf, sizeof(buf), "%.1f", t);
					dl->AddText(ImVec2(x + 2.f, oy + 2.f), IM_COL32(220, 220, 220, 255), buf);
				}
			}
		}

		void UIAnimationEditor::DrawTrackRow(
			ImDrawList* dl, UIAnimationClip& clip,
			int ctIdx, int ptIdx,
			float ox, float rowY, float rowH, float duration)
		{
			if (ctIdx >= (int)clip.clipTracks.size()) return;
			auto& ct = clip.clipTracks[ctIdx];
			if (ptIdx >= (int)ct.tracks.size()) return;
			auto& pt = ct.tracks[ptIdx];

			const bool   rowSel = (ctIdx == selClipTrackIdx_ && ptIdx == selPropTrackIdx_);
			const ImVec2 mouse  = ImGui::GetMousePos();

			// ラベル列 (ox - kLabelW .. ox) — content スクロールに乗る
			const ImU32 lblBg = rowSel ? IM_COL32(50, 80, 120, 210) : IM_COL32(35, 38, 48, 210);
			dl->AddRectFilled(ImVec2(ox - kLabelW, rowY), ImVec2(ox, rowY + rowH), lblBg);
			// ラベルテキストは window 左端固定 (スクロールしても読める)
			dl->AddText(ImVec2(ox - kLabelW + 4.f, rowY + 4.f),
			            IM_COL32(200, 215, 235, 255),
			            UIAnimationSerializer::PropertyToStr(pt.property));

			// タイムライン列 (ox ..)
			const float trackW  = duration * zoomPxPerSec_;
			const ImU32 trackBg = rowSel ? IM_COL32(40, 70, 110, 180) : IM_COL32(28, 28, 38, 160);
			dl->AddRectFilled(ImVec2(ox, rowY), ImVec2(ox + trackW, rowY + rowH), trackBg);
			dl->AddLine(ImVec2(ox, rowY + rowH - 1),
			            ImVec2(ox + trackW, rowY + rowH - 1),
			            IM_COL32(55, 55, 75, 180));

			// ラベル列クリックで選択
			const bool inLabel = mouse.x >= ox - kLabelW && mouse.x < ox &&
			                     mouse.y >= rowY           && mouse.y < rowY + rowH;
			if (inLabel && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
			{
				selClipTrackIdx_ = ctIdx;
				selPropTrackIdx_ = ptIdx;
				selKeyframeIdx_  = -1;
			}

			// キーフレーム描画 & クリック
			const float cy = rowY + rowH * 0.5f;
			bool anyKfRightClicked = false;
			for (int ki = 0; ki < (int)pt.keyframes.size(); ++ki)
			{
				auto& kf = pt.keyframes[ki];
				const float kx  = ox + kf.time * zoomPxPerSec_;
				const bool  kSel = (ctIdx == selClipTrackIdx_ &&
				                    ptIdx == selPropTrackIdx_  &&
				                    ki    == selKeyframeIdx_);

				// 菱形 (ドラッグ中は半透明)
				const ImU32 kCol = kSel
					? (isDraggingKf_ ? IM_COL32(255, 220, 50, 140) : IM_COL32(255, 220, 50, 255))
					: IM_COL32(120, 200, 120, 255);
				dl->AddQuadFilled(
					ImVec2(kx,             cy - kDiamondR),
					ImVec2(kx + kDiamondR, cy),
					ImVec2(kx,             cy + kDiamondR),
					ImVec2(kx - kDiamondR, cy),
					kCol);
				dl->AddQuad(
					ImVec2(kx,             cy - kDiamondR),
					ImVec2(kx + kDiamondR, cy),
					ImVec2(kx,             cy + kDiamondR),
					ImVec2(kx - kDiamondR, cy),
					IM_COL32(255, 255, 255, 100));

				const float kHit = kDiamondR + 2.f;
				const bool inKf = mouse.x >= kx - kHit && mouse.x <= kx + kHit &&
				                  mouse.y >= cy - kHit && mouse.y <= cy + kHit;

				// 左クリックで選択 & ドラッグ開始ソース記録
				if (inKf && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
				{
					selClipTrackIdx_ = ctIdx;
					selPropTrackIdx_ = ptIdx;
					selKeyframeIdx_  = ki;
					dragKfCtIdx_     = ctIdx;
					dragKfPtIdx_     = ptIdx;
					dragKfKiIdx_     = ki;
				}

				// 右クリック → キーフレームコンテキストメニュー
				if (inKf && ImGui::IsMouseClicked(1) && ImGui::IsWindowHovered() && !isDraggingKf_)
				{
					selClipTrackIdx_  = ctIdx;
					selPropTrackIdx_  = ptIdx;
					selKeyframeIdx_   = ki;
					anyKfRightClicked = true;
					ImGui::OpenPopup("kf_ctx");
				}

				// Tooltip (ドラッグ中は非表示)
				if (inKf && !isDraggingKf_)
				{
					ImGui::BeginTooltip();
					ImGui::Text("t=%.3f  v=%.3f  ease=%s",
					            kf.time, kf.value,
					            UIAnimationSerializer::EaseToStr(kf.ease));
					ImGui::EndTooltip();
				}
			}

			// 空トラック領域の右クリック → キー追加コンテキストメニュー
			const bool inTrackArea = mouse.x >= ox && mouse.x <= ox + trackW &&
			                         mouse.y >= rowY && mouse.y < rowY + rowH;
			if (!anyKfRightClicked && inTrackArea &&
			    ImGui::IsMouseClicked(1) && ImGui::IsWindowHovered())
			{
				selClipTrackIdx_ = ctIdx;
				selPropTrackIdx_ = ptIdx;
				selKeyframeIdx_  = -1;
				ctxClickTime_    = std::clamp((mouse.x - ox) / zoomPxPerSec_, 0.f, duration);
				ImGui::OpenPopup("track_ctx");
			}
		}


		// ---- Keyframe Inspector ---------------------------------------------

		void UIAnimationEditor::DrawKeyframeInspector(UIAnimationClip& clip)
		{
			ImGui::Text("Keyframe");
			ImGui::Separator();

			if (selClipTrackIdx_ < 0 || selClipTrackIdx_ >= (int)clip.clipTracks.size() ||
			    selPropTrackIdx_ < 0 ||
			    selKeyframeIdx_  < 0)
			{
				ImGui::TextDisabled("キーフレームを選択してください");
				return;
			}

			auto& ct = clip.clipTracks[selClipTrackIdx_];
			if (selPropTrackIdx_ >= (int)ct.tracks.size()) return;
			auto& pt = ct.tracks[selPropTrackIdx_];
			if (selKeyframeIdx_ >= (int)pt.keyframes.size()) return;

			auto& kf = pt.keyframes[selKeyframeIdx_];

			ImGui::SetNextItemWidth(100.f);
			ImGui::DragFloat("Time",  &kf.time,  0.001f, 0.f, clip.duration, "%.3f");
			ImGui::SetNextItemWidth(100.f);
			ImGui::DragFloat("Value", &kf.value, 0.01f,  -9999.f, 9999.f,  "%.3f");

			static const char* kEaseLabels[] =
				{ "Linear","EaseIn","EaseOut","EaseInOut","Bezier" };
			int easeIdx = static_cast<int>(kf.ease);
			ImGui::SetNextItemWidth(120.f);
			if (ImGui::Combo("Ease", &easeIdx, kEaseLabels, 5))
				kf.ease = static_cast<EaseType>(easeIdx);

			// Delete ボタン or Delete キー
			const bool deletePressed =
				ImGui::SmallButton("Delete Keyframe") ||
				(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				 ImGui::IsKeyPressed(ImGuiKey_Delete));

			if (deletePressed)
			{
				pt.keyframes.erase(pt.keyframes.begin() + selKeyframeIdx_);
				selKeyframeIdx_ = -1;
				return;
			}

			// 時刻順ソート (編集後)
			ImGui::SameLine();
			if (ImGui::SmallButton("Sort"))
			{
				std::sort(pt.keyframes.begin(), pt.keyframes.end(),
				          [](const UIKeyframe& a, const UIKeyframe& b)
				          { return a.time < b.time; });
				selKeyframeIdx_ = -1;
			}
		}


		// ---- Preview Controls -----------------------------------------------

		void UIAnimationEditor::DrawPreviewControls(
			UIObject* obj, UIAnimationClip& clip, float dt)
		{
			ImGui::Text("Preview");
			ImGui::Separator();

			// 再生更新
			if (isPlaying_)
			{
				scrubTime_ += dt * playSpeed_;
				if (scrubTime_ > clip.duration)
				{
					scrubTime_ = clip.duration;
					isPlaying_ = false;
				}
				if (!hasSnapshot_) { TakeSnapshot(obj, clip); hasSnapshot_ = true; }
				ApplyScrub(obj, clip);
			}

			// スクラブスライダー
			ImGui::SetNextItemWidth(200.f);
			if (ImGui::SliderFloat("##scrub", &scrubTime_, 0.f, clip.duration, "%.3f"))
			{
				isPlaying_ = false;
				if (!hasSnapshot_) { TakeSnapshot(obj, clip); hasSnapshot_ = true; }
				ApplyScrub(obj, clip);
			}
			ImGui::SameLine();
			ImGui::Text("/ %.2f", clip.duration);

			// ボタン
			if (isPlaying_)
			{
				if (ImGui::Button("||")) isPlaying_ = false;
			}
			else
			{
				if (ImGui::Button("|<"))
				{
					scrubTime_ = 0.f;
					if (!hasSnapshot_) { TakeSnapshot(obj, clip); hasSnapshot_ = true; }
					ApplyScrub(obj, clip);
				}
				ImGui::SameLine();
				if (ImGui::Button(">"))
				{
					if (!hasSnapshot_) { TakeSnapshot(obj, clip); hasSnapshot_ = true; }
					isPlaying_ = true;
				}
				ImGui::SameLine();
				if (ImGui::Button(">|"))
				{
					scrubTime_ = clip.duration;
					if (!hasSnapshot_) { TakeSnapshot(obj, clip); hasSnapshot_ = true; }
					ApplyScrub(obj, clip);
				}
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(60.f);
			ImGui::DragFloat("x", &playSpeed_, 0.05f, 0.1f, 5.f, "%.1f");

			// スクラブ時刻でキーフレーム追加
			ImGui::SameLine();
			const bool canAddKey = selClipTrackIdx_ >= 0 &&
			                       selClipTrackIdx_ < (int)clip.clipTracks.size() &&
			                       selPropTrackIdx_ >= 0 &&
			                       selPropTrackIdx_ < (int)clip.clipTracks[selClipTrackIdx_].tracks.size();
			if (!canAddKey) ImGui::BeginDisabled();
			if (ImGui::Button("+ Key"))
			{
				auto& ct = clip.clipTracks[selClipTrackIdx_];
				auto& pt = ct.tracks[selPropTrackIdx_];
				float val = obj ? pt.ReadFrom(obj) : pt.Sample(scrubTime_);
				UIKeyframe kf{ scrubTime_, val, EaseType::Linear };
				pt.keyframes.push_back(kf);
				std::sort(pt.keyframes.begin(), pt.keyframes.end(),
				          [](const UIKeyframe& a, const UIKeyframe& b)
				          { return a.time < b.time; });
				for (int ni = 0; ni < (int)pt.keyframes.size(); ++ni)
				{
					if (std::abs(pt.keyframes[ni].time - scrubTime_) < 1e-5f)
					{ selKeyframeIdx_ = ni; break; }
				}
			}
			if (!canAddKey) ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("選択トラックにスクラブ時刻でキーフレームを追加");

			// 復元ボタン
			if (hasSnapshot_)
			{
				ImGui::SameLine();
				if (ImGui::Button("Reset"))
				{
					isPlaying_ = false;
					RestoreSnapshot(obj);
					hasSnapshot_ = false;
					scrubTime_   = 0.f;
				}
			}
		}


		// ---- Save / Load ----------------------------------------------------

		void UIAnimationEditor::DrawSaveLoad(UIObject* obj)
		{
			ImGui::Text("Save / Load");
			ImGui::Separator();

			ImGui::SetNextItemWidth(260.f);
			ImGui::InputText("JSON Path", savePathBuf_, sizeof(savePathBuf_));

			auto* anim = obj->GetComponent<UIAnimationComponent>();

			ImGui::SameLine();
			if (ImGui::Button("Save") && anim)
			{
				// 既存 JSON をロードしてマージ後に書き直す
				const std::string path(savePathBuf_);
				util::JsonValue root = util::JsonParser::ParseFile(path.c_str());
				if (root.IsNull()) root = util::JsonValue::MakeObject();

				root.Set("animation", UIAnimationSerializer::SaveAll(*anim));

				if (util::JsonSerializer::WriteFile(path.c_str(), root))
					std::snprintf(statusMsg_, sizeof(statusMsg_), "Saved: %s", path.c_str());
				else
					std::snprintf(statusMsg_, sizeof(statusMsg_), "Save FAILED: %s", path.c_str());
			}

			ImGui::SameLine();
			if (ImGui::Button("Load") && anim)
			{
				const std::string path(savePathBuf_);
				util::JsonValue root = util::JsonParser::ParseFile(path.c_str());
				if (!root.IsNull() && !root["animation"].IsNull())
				{
					UIAnimationSerializer::LoadAll(root["animation"], *anim);
					std::snprintf(statusMsg_, sizeof(statusMsg_), "Loaded: %s", path.c_str());
				}
				else
				{
					std::snprintf(statusMsg_, sizeof(statusMsg_), "Load FAILED: %s", path.c_str());
				}
			}

			if (statusMsg_[0] != '\0')
				ImGui::TextColored(ImVec4(0.5f, 1.f, 0.5f, 1.f), "%s", statusMsg_);
		}


		// ---- Preview helpers ------------------------------------------------

		void UIAnimationEditor::TakeSnapshot(UIObject* obj, const UIAnimationClip& clip)
		{
			snapshot_.clear();
			for (const auto& ct : clip.clipTracks)
			{
				for (const auto& pt : ct.tracks)
				{
					if (snapshot_.find(pt.property) == snapshot_.end())
						snapshot_[pt.property] = pt.ReadFrom(obj);
				}
			}
		}

		void UIAnimationEditor::RestoreSnapshot(UIObject* obj)
		{
			for (auto& [prop, val] : snapshot_)
			{
				UIAnimationTrack tmp;
				tmp.property = prop;
				tmp.Apply(obj, val);
			}
		}

		void UIAnimationEditor::ApplyScrub(UIObject* obj, const UIAnimationClip& clip)
		{
			for (const auto& ct : clip.clipTracks)
			{
				for (const auto& pt : ct.tracks)
				{
					float v = pt.Sample(scrubTime_);
					pt.Apply(obj, v);
				}
			}
		}

	} // namespace ui
} // namespace aq
#endif
