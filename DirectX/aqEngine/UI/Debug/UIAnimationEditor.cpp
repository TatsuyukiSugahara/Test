#include "aq.h"
#include "UIAnimationEditor.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#include "UIEditorSession.h"
#include "UI/UIObject.h"
#include "UI/Component/UIAnimationComponent.h"
#include "UI/Animation/UIAnimationSerializer.h"
#include "UI/Animation/UIAnimationClip.h"
#include "UI/Animation/UIAnimationTrack.h"
#include <cstdio>
#include <cmath>

namespace aq
{
	namespace ui
	{
		static constexpr float ROW_H         = 22.f;
		static constexpr float LABEL_W       = 170.f;
		static constexpr float RULER_H       = 24.f;
		static constexpr float DIAMOND_R     = 5.f;
		static constexpr float MIN_ZOOM      = 40.f;
		static constexpr float MAX_ZOOM      = 800.f;


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

			if (ImGui::SmallButton(windowPinned_ ? "[Pinned] Unpin" : "[Pin]"))
				windowPinned_ = !windowPinned_;
			ImGui::SameLine();

			auto& session = UIEditorSession::Get();

			// UI Editor の Hierarchy 選択が変わったら Clip / Keyframe 選択とプレビューを捨てる
			if (session.selectedObject != prevTargetHandle_)
			{
				UIObject* prevTarget = UIContext::Get().Resolve(prevTargetHandle_);
				OnTargetChanged(prevTarget);
				prevTargetHandle_ = session.selectedObject;
			}

			UIObject* target = UIContext::Get().Resolve(session.selectedObject);

			ImGui::Text("Target: %s", target ? target->GetName().data() : "(none)");
			ImGui::Separator();

			if (!target)
			{
				ImGui::TextDisabled("Select an object in UI Editor");
				ImGui::End();
				return;
			}

			auto* anim = target->GetComponent<UIAnimationComponent>();
			if (!anim)
			{
				ImGui::TextColored(ImVec4(1,0.6f,0,1), "No UIAnimationComponent");
				if (ImGui::Button("Add Component"))
					anim = target->AddComponent<UIAnimationComponent>();
				if (!anim) { ImGui::End(); return; }
			}

			// 選択 Clip が範囲外なら解除
			if (selClipIdx_ >= (int)anim->GetClips().size())
			{
				selClipIdx_     = -1;
				prevSelClipIdx_ = -1;
			}

			// 左右分割
			const float leftW  = 260.f;
			const float totalH = ImGui::GetContentRegionAvail().y - 120.f; // 下部パネル分を確保

			ImGui::BeginChild("##left", ImVec2(leftW, totalH), true);
			DrawClipPanel(target);
			ImGui::EndChild();

			ImGui::SameLine();

			ImGui::BeginChild("##right", ImVec2(0, totalH), true);
			if (selClipIdx_ >= 0)
			{
				auto& clip = anim->EditClip(static_cast<size_t>(selClipIdx_));
				DrawTimeline(target, clip);
			}
			else
			{
				ImGui::TextDisabled("Select a clip");
			}
			ImGui::EndChild();

			ImGui::Separator();

			// 下部: キーフレーム Inspector + Preview Controls
			ImGui::BeginChild("##bottom", ImVec2(0, 0));
			{
				if (selClipIdx_ >= 0)
				{
					auto& clip = anim->EditClip(static_cast<size_t>(selClipIdx_));

					// キーフレーム Inspector (左)
					ImGui::BeginChild("##kfinspector", ImVec2(300, 0), false);
					DrawKeyframeInspector(clip);
					ImGui::EndChild();

					ImGui::SameLine();

					// Preview Controls (残り全部)
					ImGui::BeginChild("##preview", ImVec2(0, 0), false);
					DrawPreviewControls(target, clip, dt);
					ImGui::EndChild();
				}
			}
			ImGui::EndChild();

			ImGui::End();
		}


		// ---- Selection --------------------------------------------------------

		// UI Editor の Hierarchy 選択が変わった時の後始末。
		// 旧オブジェクトのプレビュースナップショットを戻し、Clip / Keyframe 選択を捨てる。
		void UIAnimationEditor::OnTargetChanged(UIObject* prevObj)
		{
			selClipIdx_     = -1;
			prevSelClipIdx_ = -1;
			selTrackIdx_    = -1;
			selKeyframeIdx_ = -1;
			if (hasSnapshot_)
			{
				if (prevObj)
					RestoreSnapshot(prevObj);
				hasSnapshot_ = false;
			}
			isPlaying_ = false;
		}


		// ---- Left Panel: Clip + Track List ------------------------------------

		void UIAnimationEditor::DrawClipPanel(UIObject* obj)
		{
			auto* anim = obj->GetComponent<UIAnimationComponent>();
			if (!anim) return;

			const auto& clips = anim->GetClips();

			// ---- Clip selector ----
			ImGui::Text("Clips");
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Clip"))
			{
				UIAnimationClip nc;
				nc.name     = "NewClip";
				nc.duration = 1.f;

				// 名前重複回避
				auto nameExists = [&clips](const std::string& n)
					{
						for (const auto& c : clips) { if (c.name == n) return true; }
						return false;
					};
				int idx = 1;
				while (nameExists(nc.name))
					nc.name = "NewClip" + std::to_string(idx++);

				const size_t newIdx = anim->AddClip(std::move(nc));
				selClipIdx_  = static_cast<int>(newIdx);
				selTrackIdx_ = selKeyframeIdx_ = -1;
				UIEditorSession::Get().dirty = true;
			}

			ImGui::Separator();

			for (int i = 0; i < (int)clips.size(); ++i)
			{
				ImGui::PushID(i);
				const bool sel = (i == selClipIdx_);
				if (ImGui::Selectable(clips[i].name.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick))
				{
					if (selClipIdx_ != i)
					{
						selClipIdx_     = i;
						selTrackIdx_    = selKeyframeIdx_ = -1;
						scrubTime_      = 0.f;
						isPlaying_      = false;
						if (hasSnapshot_) { RestoreSnapshot(obj); hasSnapshot_ = false; }
					}
				}
				ImGui::PopID();
			}

			if (selClipIdx_ < 0 || selClipIdx_ >= (int)clips.size())
				return;

			auto& clip = anim->EditClip(static_cast<size_t>(selClipIdx_));

			// 選択クリップが変わったときだけバッファを同期する
			if (selClipIdx_ != prevSelClipIdx_)
			{
				std::snprintf(clipNameBuf_,  sizeof(clipNameBuf_),  "%s", clip.name.c_str());
				std::snprintf(clipParamBuf_, sizeof(clipParamBuf_), "%s", clip.conditionParamName.c_str());
				std::snprintf(clipGroupBuf_, sizeof(clipGroupBuf_), "%s", clip.groupName.c_str());
				prevSelClipIdx_ = selClipIdx_;
			}

			ImGui::Separator();
			ImGui::Text("Clip: %s", clip.name.c_str());

			// Name
			ImGui::SetNextItemWidth(120.f);
			if (ImGui::InputText("Name##clip", clipNameBuf_, sizeof(clipNameBuf_),
			                     ImGuiInputTextFlags_EnterReturnsTrue))
			{
				const std::string newName(clipNameBuf_);
				if (!newName.empty())
				{
					clip.name = newName;
					// groupName が空のクリップは group = aqHash32(name) なので、改名で group も引き直す (§4.3)
					if (clip.groupName.empty())
						anim->SetClipGroup(static_cast<size_t>(selClipIdx_), clip.groupName);
					UIEditorSession::Get().dirty = true;
				}
			}

			// Duration
			ImGui::SetNextItemWidth(80.f);
			ImGui::DragFloat("Duration", &clip.duration, 0.01f, 0.01f, 60.f, "%.2fs");
			if (ImGui::IsItemEdited())
				UIEditorSession::Get().dirty = true;
			clip.duration = std::max(clip.duration, 0.01f);

			// Cond
			static const char* COND_LABELS[] = { "Manual", "Bool", "Trigger" };
			int condIdx = static_cast<int>(clip.condition);
			ImGui::SetNextItemWidth(80.f);
			if (ImGui::Combo("Cond", &condIdx, COND_LABELS, 3))
			{
				const auto newCond = static_cast<UIClipCondition>(condIdx);
				anim->SetClipCondition(static_cast<size_t>(selClipIdx_), newCond,
				                       aqHash32(clipParamBuf_), clipParamBuf_);
				UIEditorSession::Get().dirty = true;
			}

			// Param (Bool / Trigger のときだけ)
			if (clip.condition == UIClipCondition::Bool || clip.condition == UIClipCondition::Trigger)
			{
				ImGui::SetNextItemWidth(100.f);
				if (ImGui::InputText("Param##clip", clipParamBuf_, sizeof(clipParamBuf_),
				                     ImGuiInputTextFlags_EnterReturnsTrue))
				{
					anim->SetClipCondition(static_cast<size_t>(selClipIdx_), clip.condition,
					                       aqHash32(clipParamBuf_), clipParamBuf_);
					UIEditorSession::Get().dirty = true;
				}
			}

			// Group (Manual のときだけ)
			if (clip.condition == UIClipCondition::Manual)
			{
				ImGui::SetNextItemWidth(100.f);
				if (ImGui::InputText("Group##clip", clipGroupBuf_, sizeof(clipGroupBuf_),
				                     ImGuiInputTextFlags_EnterReturnsTrue))
				{
					anim->SetClipGroup(static_cast<size_t>(selClipIdx_), clipGroupBuf_);
					UIEditorSession::Get().dirty = true;
				}
				if (clip.groupName.empty())
				{
					ImGui::SameLine();
					ImGui::TextDisabled("(= %s)", clip.name.c_str());
				}
			}

			// Loop
			bool loop = (clip.loopFrom >= 0.f);
			if (ImGui::Checkbox("Loop", &loop))
			{
				clip.loopFrom = loop ? 0.f : -1.f;
				UIEditorSession::Get().dirty = true;
			}
			if (loop)
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(60.f);
				ImGui::DragFloat("LoopFrom", &clip.loopFrom, 0.01f, 0.f, clip.duration, "%.2f");
				if (ImGui::IsItemEdited())
					UIEditorSession::Get().dirty = true;
				clip.loopFrom = std::clamp(clip.loopFrom, 0.f, clip.duration);
			}

			// Finish
			static const char* FINISH_LABELS[] = { "Hold", "Restore" };
			int finishIdx = static_cast<int>(clip.finish);
			ImGui::SetNextItemWidth(90.f);
			if (ImGui::Combo("Finish", &finishIdx, FINISH_LABELS, 2))
			{
				clip.finish = static_cast<UIAnimationFinishMode>(finishIdx);
				UIEditorSession::Get().dirty = true;
			}

			if (ImGui::SmallButton("- Clip"))
			{
				if (hasSnapshot_) { RestoreSnapshot(obj); hasSnapshot_ = false; }
				anim->RemoveClip(static_cast<size_t>(selClipIdx_));
				selClipIdx_     = -1;
				prevSelClipIdx_ = -1;
				selTrackIdx_    = selKeyframeIdx_ = -1;
				UIEditorSession::Get().dirty = true;
				return;
			}

			ImGui::Separator();
			DrawTrackList(clip);
		}

		void UIAnimationEditor::DrawTrackList(UIAnimationClip& clip)
		{
			ImGui::Text("Tracks");
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Track"))
			{
				UIAnimationTrack track;
				track.property = UIAnimatedProperty::PositionX;
				clip.tracks.push_back(std::move(track));
				selTrackIdx_    = static_cast<int>(clip.tracks.size()) - 1;
				selKeyframeIdx_ = -1;
				UIEditorSession::Get().dirty = true;
			}

			static const char* PROP_LABELS[] = {
				"PositionX","PositionY","PositionZ",
				"ScaleX","ScaleY","Rotation",
				"SizeDeltaX","SizeDeltaY",
				"ColorR","ColorG","ColorB","ColorA",
				"FillAmount","Active",
				"NineSliceBorderLeft","NineSliceBorderRight",
				"NineSliceBorderTop","NineSliceBorderBottom",
				"TextCharCount",
			};
			static const UIAnimatedProperty PROP_VALUES[] = {
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
			constexpr int PROP_COUNT = 19;

			for (int ti = 0; ti < (int)clip.tracks.size(); ++ti)
			{
				auto& track = clip.tracks[ti];
				ImGui::PushID(ti);

				const bool trackSel = (ti == selTrackIdx_);
				char trackLabel[64];
				std::snprintf(trackLabel, sizeof(trackLabel), "%s",
				              UIAnimationSerializer::PropertyToStr(track.property));

				if (ImGui::Selectable(trackLabel, trackSel, ImGuiSelectableFlags_AllowOverlap))
				{
					selTrackIdx_    = ti;
					selKeyframeIdx_ = -1;
				}
				ImGui::SameLine(120.f);

				// Property ドロップダウン
				int propIdx = 0;
				for (int k = 0; k < PROP_COUNT; ++k)
				{
					if (PROP_VALUES[k] == track.property) { propIdx = k; break; }
				}
				ImGui::SetNextItemWidth(140.f);
				if (ImGui::Combo("##prop", &propIdx, PROP_LABELS, PROP_COUNT))
				{
					track.property = PROP_VALUES[propIdx];
					UIEditorSession::Get().dirty = true;
				}

				ImGui::SameLine();
				if (ImGui::SmallButton("-##track"))
				{
					clip.tracks.erase(clip.tracks.begin() + ti);
					if (selTrackIdx_ >= (int)clip.tracks.size())
						selTrackIdx_ = (int)clip.tracks.size() - 1;
					selKeyframeIdx_ = -1;
					UIEditorSession::Get().dirty = true;
					ImGui::PopID();
					continue;
				}

				ImGui::PopID();
			}
		}


		// ---- Timeline -------------------------------------------------------

		void UIAnimationEditor::DrawTimeline(UIObject* obj, UIAnimationClip& clip)
		{
			const float totalDur = clip.duration;
			const float totalW   = totalDur * zoomPxPerSec_;

			const int totalRows = (int)clip.tracks.size();

			const float contentW = LABEL_W + totalW + 20.f;
			const float contentH = RULER_H + totalRows * ROW_H + 10.f;

			// 操作ヒント
			ImGui::TextDisabled("Right click: add/remove key  |  Ctrl+Click: add key  |  Delete: remove  |  Drag: move  |  Wheel: zoom");

			// 横スクロール可能な子ウィンドウ
			ImGui::BeginChild("##tlscroll", ImVec2(0, 0), false,
			                  ImGuiWindowFlags_HorizontalScrollbar);

			// マウスホイールでズーム (window hovered 時のみ)
			if (ImGui::IsWindowHovered())
			{
				float wheel = ImGui::GetIO().MouseWheel;
				if (wheel != 0.f)
					zoomPxPerSec_ = std::clamp(zoomPxPerSec_ * (1.f + wheel * 0.1f),
					                            MIN_ZOOM, MAX_ZOOM);
			}

			ImDrawList* dl  = ImGui::GetWindowDrawList();
			const ImVec2 wp = ImGui::GetWindowPos();
			const float  sx = ImGui::GetScrollX();
			const float  sy = ImGui::GetScrollY();

			// content 座標 -> screen 座標: screen_x = wp.x + content_x - sx
			// ラベル列: content x = 0 .. LABEL_W (スクロール時は画面外へ)
			// タイムライン: content x = LABEL_W ..
			const float tlX = wp.x + LABEL_W - sx;  // タイムライン開始 screen x
			const float tlY = wp.y - sy;             // 先頭 screen y

			// 全体背景
			dl->AddRectFilled(ImVec2(wp.x, wp.y),
			                  ImVec2(wp.x + ImGui::GetWindowWidth(),
			                         wp.y + ImGui::GetWindowHeight()),
			                  IM_COL32(22, 22, 30, 255));

			// ルーラー描画
			DrawRuler(dl, tlX, tlY, totalW, totalDur);

			// トラック行描画 (ClipTrack ヘッダは廃止。clip.tracks を直接並べる)
			float rowY = tlY + RULER_H;
			for (int ti = 0; ti < (int)clip.tracks.size(); ++ti)
			{
				DrawTrackRow(dl, clip, ti, tlX, rowY, ROW_H, totalDur);
				rowY += ROW_H;
			}

			// スクラバー縦線 + 三角ヘッド
			const float scrubX = tlX + scrubTime_ * zoomPxPerSec_;
			dl->AddLine(ImVec2(scrubX, tlY + RULER_H),
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
			                     mouse.y >= tlY && mouse.y <= tlY + RULER_H;

			if (inRuler && ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
			{
				float t = (mouse.x - tlX) / zoomPxPerSec_;
				scrubTime_ = std::clamp(t, 0.f, totalDur);
				if (!hasSnapshot_) { TakeSnapshot(obj, clip); hasSnapshot_ = true; }
				ApplyScrub(obj, clip);
			}

			// Ctrl+クリックで選択 Track にキーフレーム追加
			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) &&
			    ImGui::GetIO().KeyCtrl &&
			    selTrackIdx_ >= 0 && selTrackIdx_ < (int)clip.tracks.size())
			{
				float t = std::clamp((mouse.x - tlX) / zoomPxPerSec_, 0.f, totalDur);
				auto& track = clip.tracks[selTrackIdx_];
				UIKeyframe kf{ t, track.ReadFrom(obj), EaseType::Linear };
				track.keyframes.push_back(kf);
				std::sort(track.keyframes.begin(), track.keyframes.end(),
				          [](const UIKeyframe& a, const UIKeyframe& b)
				          { return a.time < b.time; });
				UIEditorSession::Get().dirty = true;
			}

			// キーフレームドラッグ処理 (クリック開始位置がキーフレーム上のときのみ動く)
			if (dragKfTrackIdx_ >= 0 && dragKfKiIdx_ >= 0 &&
			    dragKfTrackIdx_ < (int)clip.tracks.size())
			{
				auto& dtrack = clip.tracks[dragKfTrackIdx_];
				if (dragKfKiIdx_ < (int)dtrack.keyframes.size())
				{
					auto& dkf = dtrack.keyframes[dragKfKiIdx_];
					if (ImGui::IsMouseDragging(0, 2.f))
					{
						isDraggingKf_ = true;
						const float dx = ImGui::GetIO().MouseDelta.x;
						dkf.time = std::clamp(dkf.time + dx / zoomPxPerSec_, 0.f, totalDur);
					}
					if (isDraggingKf_ && ImGui::IsMouseReleased(0))
					{
						const float savedTime = dkf.time;
						std::sort(dtrack.keyframes.begin(), dtrack.keyframes.end(),
						          [](const UIKeyframe& a, const UIKeyframe& b)
						          { return a.time < b.time; });
						for (int ni = 0; ni < (int)dtrack.keyframes.size(); ++ni)
						{
							if (std::abs(dtrack.keyframes[ni].time - savedTime) < 1e-5f)
							{ selKeyframeIdx_ = ni; break; }
						}
						UIEditorSession::Get().dirty = true;
					}
				}
			}
			if (ImGui::IsMouseReleased(0))
			{
				isDraggingKf_   = false;
				dragKfTrackIdx_ = dragKfKiIdx_ = -1;
			}

			// Deleteキー → 選択キーフレーム削除
			if (ImGui::IsWindowHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
			    selTrackIdx_ >= 0 && selTrackIdx_ < (int)clip.tracks.size() && selKeyframeIdx_ >= 0)
			{
				auto& track = clip.tracks[selTrackIdx_];
				if (selKeyframeIdx_ < (int)track.keyframes.size())
				{
					track.keyframes.erase(track.keyframes.begin() + selKeyframeIdx_);
					selKeyframeIdx_ = -1;
					UIEditorSession::Get().dirty = true;
				}
			}

			// キーフレームコンテキストメニュー
			if (ImGui::BeginPopup("kf_ctx"))
			{
				if (selTrackIdx_ >= 0 && selTrackIdx_ < (int)clip.tracks.size() && selKeyframeIdx_ >= 0)
				{
					auto& track = clip.tracks[selTrackIdx_];
					if (selKeyframeIdx_ < (int)track.keyframes.size())
					{
						auto& kf = track.keyframes[selKeyframeIdx_];
						ImGui::TextDisabled("t=%.3f  v=%.3f", kf.time, kf.value);
						ImGui::Separator();
						static const char* EASE_MENU_LABELS[] =
							{ "Linear","EaseIn","EaseOut","EaseInOut","Bezier" };
						if (ImGui::BeginMenu("Ease"))
						{
							for (int ei = 0; ei < 5; ++ei)
							{
								bool isCur = (static_cast<int>(kf.ease) == ei);
								if (ImGui::MenuItem(EASE_MENU_LABELS[ei], nullptr, isCur))
								{
									kf.ease = static_cast<EaseType>(ei);
									UIEditorSession::Get().dirty = true;
								}
							}
							ImGui::EndMenu();
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Duplicate Key"))
						{
							UIKeyframe dup = kf;
							dup.time = std::min(dup.time + 0.1f, clip.duration);
							track.keyframes.push_back(dup);
							std::sort(track.keyframes.begin(), track.keyframes.end(),
							          [](const UIKeyframe& a, const UIKeyframe& b)
							          { return a.time < b.time; });
							for (int ni = 0; ni < (int)track.keyframes.size(); ++ni)
							{
								if (std::abs(track.keyframes[ni].time - dup.time) < 1e-5f)
								{ selKeyframeIdx_ = ni; break; }
							}
							UIEditorSession::Get().dirty = true;
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Delete Key"))
						{
							track.keyframes.erase(track.keyframes.begin() + selKeyframeIdx_);
							selKeyframeIdx_ = -1;
							UIEditorSession::Get().dirty = true;
						}
					}
				}
				ImGui::EndPopup();
			}

			// トラック空領域コンテキストメニュー
			if (ImGui::BeginPopup("track_ctx"))
			{
				if (selTrackIdx_ >= 0 && selTrackIdx_ < (int)clip.tracks.size())
				{
					auto& track = clip.tracks[selTrackIdx_];
					ImGui::TextDisabled("%s  t=%.3f",
					    UIAnimationSerializer::PropertyToStr(track.property), ctxClickTime_);
					ImGui::Separator();
					if (ImGui::MenuItem("Add Key here"))
					{
						UIKeyframe kf{ ctxClickTime_, track.Sample(ctxClickTime_), EaseType::Linear };
						track.keyframes.push_back(kf);
						std::sort(track.keyframes.begin(), track.keyframes.end(),
						          [](const UIKeyframe& a, const UIKeyframe& b)
						          { return a.time < b.time; });
						for (int ni = 0; ni < (int)track.keyframes.size(); ++ni)
						{
							if (std::abs(track.keyframes[ni].time - ctxClickTime_) < 1e-5f)
							{ selKeyframeIdx_ = ni; break; }
						}
						UIEditorSession::Get().dirty = true;
					}
					if (ImGui::MenuItem("Add Key at Scrub"))
					{
						UIKeyframe kf{ scrubTime_, track.Sample(scrubTime_), EaseType::Linear };
						track.keyframes.push_back(kf);
						std::sort(track.keyframes.begin(), track.keyframes.end(),
						          [](const UIKeyframe& a, const UIKeyframe& b)
						          { return a.time < b.time; });
						for (int ni = 0; ni < (int)track.keyframes.size(); ++ni)
						{
							if (std::abs(track.keyframes[ni].time - scrubTime_) < 1e-5f)
							{ selKeyframeIdx_ = ni; break; }
						}
						UIEditorSession::Get().dirty = true;
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
			                  ImVec2(ox + w, oy + RULER_H),
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
				float tickH = major ? RULER_H * 0.6f : RULER_H * 0.3f;
				dl->AddLine(ImVec2(x, oy + RULER_H - tickH),
				            ImVec2(x, oy + RULER_H),
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
			int trackIdx,
			float ox, float rowY, float rowH, float duration)
		{
			if (trackIdx >= (int)clip.tracks.size()) return;
			auto& track = clip.tracks[trackIdx];

			const bool   rowSel = (trackIdx == selTrackIdx_);
			const ImVec2 mouse  = ImGui::GetMousePos();

			// ラベル列 (ox - LABEL_W .. ox) — content スクロールに乗る
			const ImU32 lblBg = rowSel ? IM_COL32(50, 80, 120, 210) : IM_COL32(35, 38, 48, 210);
			dl->AddRectFilled(ImVec2(ox - LABEL_W, rowY), ImVec2(ox, rowY + rowH), lblBg);
			// ラベルテキストは window 左端固定 (スクロールしても読める)
			dl->AddText(ImVec2(ox - LABEL_W + 4.f, rowY + 4.f),
			            IM_COL32(200, 215, 235, 255),
			            UIAnimationSerializer::PropertyToStr(track.property));

			// タイムライン列 (ox ..)
			const float trackW  = duration * zoomPxPerSec_;
			const ImU32 trackBg = rowSel ? IM_COL32(40, 70, 110, 180) : IM_COL32(28, 28, 38, 160);
			dl->AddRectFilled(ImVec2(ox, rowY), ImVec2(ox + trackW, rowY + rowH), trackBg);
			dl->AddLine(ImVec2(ox, rowY + rowH - 1),
			            ImVec2(ox + trackW, rowY + rowH - 1),
			            IM_COL32(55, 55, 75, 180));

			// ラベル列クリックで選択
			const bool inLabel = mouse.x >= ox - LABEL_W && mouse.x < ox &&
			                     mouse.y >= rowY           && mouse.y < rowY + rowH;
			if (inLabel && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
			{
				selTrackIdx_    = trackIdx;
				selKeyframeIdx_ = -1;
			}

			// キーフレーム描画 & クリック
			const float cy = rowY + rowH * 0.5f;
			bool anyKfRightClicked = false;
			for (int ki = 0; ki < (int)track.keyframes.size(); ++ki)
			{
				auto& kf = track.keyframes[ki];
				const float kx  = ox + kf.time * zoomPxPerSec_;
				const bool  kSel = (trackIdx == selTrackIdx_ && ki == selKeyframeIdx_);

				// 菱形 (ドラッグ中は半透明)
				const ImU32 kCol = kSel
					? (isDraggingKf_ ? IM_COL32(255, 220, 50, 140) : IM_COL32(255, 220, 50, 255))
					: IM_COL32(120, 200, 120, 255);
				dl->AddQuadFilled(
					ImVec2(kx,              cy - DIAMOND_R),
					ImVec2(kx + DIAMOND_R,  cy),
					ImVec2(kx,              cy + DIAMOND_R),
					ImVec2(kx - DIAMOND_R,  cy),
					kCol);
				dl->AddQuad(
					ImVec2(kx,              cy - DIAMOND_R),
					ImVec2(kx + DIAMOND_R,  cy),
					ImVec2(kx,              cy + DIAMOND_R),
					ImVec2(kx - DIAMOND_R,  cy),
					IM_COL32(255, 255, 255, 100));

				const float kHit = DIAMOND_R + 2.f;
				const bool inKf = mouse.x >= kx - kHit && mouse.x <= kx + kHit &&
				                  mouse.y >= cy - kHit && mouse.y <= cy + kHit;

				// 左クリックで選択 & ドラッグ開始ソース記録
				if (inKf && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
				{
					selTrackIdx_    = trackIdx;
					selKeyframeIdx_ = ki;
					dragKfTrackIdx_ = trackIdx;
					dragKfKiIdx_    = ki;
				}

				// 右クリック → キーフレームコンテキストメニュー
				if (inKf && ImGui::IsMouseClicked(1) && ImGui::IsWindowHovered() && !isDraggingKf_)
				{
					selTrackIdx_      = trackIdx;
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
				selTrackIdx_    = trackIdx;
				selKeyframeIdx_ = -1;
				ctxClickTime_   = std::clamp((mouse.x - ox) / zoomPxPerSec_, 0.f, duration);
				ImGui::OpenPopup("track_ctx");
			}
		}


		// ---- Keyframe Inspector ---------------------------------------------

		void UIAnimationEditor::DrawKeyframeInspector(UIAnimationClip& clip)
		{
			ImGui::Text("Keyframe");
			ImGui::Separator();

			if (selTrackIdx_ < 0 || selTrackIdx_ >= (int)clip.tracks.size() ||
			    selKeyframeIdx_  < 0)
			{
				ImGui::TextDisabled("Select a keyframe");
				return;
			}

			auto& track = clip.tracks[selTrackIdx_];
			if (selKeyframeIdx_ >= (int)track.keyframes.size()) return;

			auto& kf = track.keyframes[selKeyframeIdx_];

			ImGui::SetNextItemWidth(100.f);
			ImGui::DragFloat("Time",  &kf.time,  0.001f, 0.f, clip.duration, "%.3f");
			if (ImGui::IsItemEdited())
				UIEditorSession::Get().dirty = true;
			ImGui::SetNextItemWidth(100.f);
			ImGui::DragFloat("Value", &kf.value, 0.01f,  -9999.f, 9999.f,  "%.3f");
			if (ImGui::IsItemEdited())
				UIEditorSession::Get().dirty = true;

			static const char* EASE_LABELS[] =
				{ "Linear","EaseIn","EaseOut","EaseInOut","Bezier" };
			int easeIdx = static_cast<int>(kf.ease);
			ImGui::SetNextItemWidth(120.f);
			if (ImGui::Combo("Ease", &easeIdx, EASE_LABELS, 5))
			{
				kf.ease = static_cast<EaseType>(easeIdx);
				UIEditorSession::Get().dirty = true;
			}

			// Delete ボタン or Delete キー
			const bool deletePressed =
				ImGui::SmallButton("Delete Keyframe") ||
				(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				 ImGui::IsKeyPressed(ImGuiKey_Delete));

			if (deletePressed)
			{
				track.keyframes.erase(track.keyframes.begin() + selKeyframeIdx_);
				selKeyframeIdx_ = -1;
				UIEditorSession::Get().dirty = true;
				return;
			}

			// 時刻順ソート (編集後)
			ImGui::SameLine();
			if (ImGui::SmallButton("Sort"))
			{
				std::sort(track.keyframes.begin(), track.keyframes.end(),
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
			const bool canAddKey = selTrackIdx_ >= 0 && selTrackIdx_ < (int)clip.tracks.size();
			if (!canAddKey) ImGui::BeginDisabled();
			if (ImGui::Button("+ Key"))
			{
				auto& track = clip.tracks[selTrackIdx_];
				float val = obj ? track.ReadFrom(obj) : track.Sample(scrubTime_);
				UIKeyframe kf{ scrubTime_, val, EaseType::Linear };
				track.keyframes.push_back(kf);
				std::sort(track.keyframes.begin(), track.keyframes.end(),
				          [](const UIKeyframe& a, const UIKeyframe& b)
				          { return a.time < b.time; });
				for (int ni = 0; ni < (int)track.keyframes.size(); ++ni)
				{
					if (std::abs(track.keyframes[ni].time - scrubTime_) < 1e-5f)
					{ selKeyframeIdx_ = ni; break; }
				}
				UIEditorSession::Get().dirty = true;
			}
			if (!canAddKey) ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a keyframe to the selected track at the scrub time");

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


		// ---- Preview helpers ------------------------------------------------

		void UIAnimationEditor::TakeSnapshot(UIObject* obj, const UIAnimationClip& clip)
		{
			snapshot_.clear();
			for (const auto& track : clip.tracks)
			{
				if (snapshot_.find(track.property) == snapshot_.end())
					snapshot_[track.property] = track.ReadFrom(obj);
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
			for (const auto& track : clip.tracks)
			{
				float v = track.Sample(scrubTime_);
				track.Apply(obj, v);
			}
		}

	} // namespace ui
} // namespace aq
#endif
