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

		// Timeline 下部 (Keyframe Inspector + Preview Controls) の固定高さと、
		// ComputeTimelineHeight() が返す高さの上下限
		static constexpr float BOTTOM_PANE_H  = 120.f;
		static constexpr float TIMELINE_MIN_H = 240.f;
		static constexpr float TIMELINE_MAX_H = 560.f;

		// Exit 待機付き画面遷移 (P2B) は完了済みなので Play when: Exit は有効。
		// false にすると Exit 項目を Disabled にできる仕組みだけ残す (設計書 §10.3)
		static constexpr bool kExitTransitionReady = true;

		// "Play when" Combo の選択肢。表示順はそのままインデックスに使う
		enum PlayWhenOption
		{
			PlayWhen_Enter = 0,
			PlayWhen_Exit,
			PlayWhen_Hover,
			PlayWhen_Pressed,
			PlayWhen_Focused,
			PlayWhen_Click,
			PlayWhen_Manual,
			PlayWhen_Advanced,
			PlayWhen_Count,
		};

		static const char* PLAY_WHEN_LABELS[PlayWhen_Count] =
		{
			"Enter", "Exit", "Hover", "Pressed", "Focused", "Click", "Manual", "Advanced",
		};


		// クリップの現在の condition / group / conditionParam から "Play when" の表示値を導く (設計書 §10.3)
		static int ComputePlayWhenIndex(const UIAnimationClip& clip)
		{
			if (clip.condition == UIClipCondition::Manual)
			{
				if (clip.groupName == "Enter") return PlayWhen_Enter;
				if (clip.groupName == "Exit")  return PlayWhen_Exit;
				return PlayWhen_Manual;
			}
			if (clip.condition == UIClipCondition::Bool)
			{
				if (clip.conditionParamName == "Hover")   return PlayWhen_Hover;
				if (clip.conditionParamName == "Pressed") return PlayWhen_Pressed;
				if (clip.conditionParamName == "Focused") return PlayWhen_Focused;
				return PlayWhen_Advanced;
			}
			if (clip.condition == UIClipCondition::Trigger)
			{
				if (clip.conditionParamName == "Click") return PlayWhen_Click;
				return PlayWhen_Advanced;
			}
			return PlayWhen_Advanced;
		}


		// Timeline 左パネルの見出し文字列。Manual は groupName (空なら name)、
		// Bool / Trigger は Play when の表示名 (Advanced 相当は conditionParamName)
		static std::string ComputeGroupHeader(const UIAnimationClip& clip)
		{
			if (clip.condition == UIClipCondition::Manual)
				return clip.groupName.empty() ? clip.name : clip.groupName;

			const int pw = ComputePlayWhenIndex(clip);
			if (pw == PlayWhen_Advanced)
				return clip.conditionParamName;
			return PLAY_WHEN_LABELS[pw];
		}


		// Timeline の表示行 (設計書 §15.1)。毎フレーム clip.tracks から組み立て直す。
		// データ (UIAnimationTrack / JSON) は変えず、表示と操作だけをまとめる
		namespace
		{
			struct TimelineRow
			{
				const char* label;       // "Position" / "PositionX" / "ColorA" ...
				int         trackIdx[2]; // 組なら X, Y の Track index。単独なら { idx, -1 }
				bool        isVector;
			};

			// X/Y に分かれたプロパティの組。両方の Track がクリップに揃ったときだけ 1 行にまとめる
			struct VectorPropertyPair
			{
				const char*        label;
				UIAnimatedProperty x;
				UIAnimatedProperty y;
			};

			static constexpr VectorPropertyPair VECTOR_PROPERTY_PAIRS[] =
			{
				{ "Position",  UIAnimatedProperty::PositionX,  UIAnimatedProperty::PositionY },
				{ "Scale",     UIAnimatedProperty::ScaleX,     UIAnimatedProperty::ScaleY },
				{ "SizeDelta", UIAnimatedProperty::SizeDeltaX, UIAnimatedProperty::SizeDeltaY },
			};


			// clip.tracks を Row の列へ組み立てる (設計書 §15.1)。行の並びは X 側 Track index の
			// 位置に従う (Y が先に並んでいても、組の行は X の位置で出る)
			std::vector<TimelineRow> BuildTimelineRows(const UIAnimationClip& clip)
			{
				auto findTrack = [&clip](UIAnimatedProperty prop) -> int
					{
						for (int i = 0; i < (int)clip.tracks.size(); ++i)
						{
							if (clip.tracks[i].property == prop) return i;
						}
						return -1;
					};

				// Track index -> 組の相方 (Y) の index。組でなければ -1
				std::vector<int>         pairYIdx(clip.tracks.size(), -1);
				std::vector<const char*> pairLabel(clip.tracks.size(), nullptr);
				// Y 側は X 側でまとめて出すので単独では出さない
				std::vector<bool>        isYSide(clip.tracks.size(), false);

				for (const auto& pair : VECTOR_PROPERTY_PAIRS)
				{
					const int xIdx = findTrack(pair.x);
					const int yIdx = findTrack(pair.y);
					if (xIdx < 0 || yIdx < 0) continue; // 片方だけなら単独行のまま

					pairYIdx[xIdx]  = yIdx;
					pairLabel[xIdx] = pair.label;
					isYSide[yIdx]   = true;
				}

				std::vector<TimelineRow> rows;
				rows.reserve(clip.tracks.size());
				for (int i = 0; i < (int)clip.tracks.size(); ++i)
				{
					if (isYSide[i]) continue;

					if (pairYIdx[i] >= 0)
						rows.push_back(TimelineRow{ pairLabel[i], { i, pairYIdx[i] }, true });
					else
						rows.push_back(TimelineRow{ UIAnimationSerializer::PropertyToStr(clip.tracks[i].property), { i, -1 }, false });
				}
				return rows;
			}


			// 時刻順ソート (Sample / シリアライズは時刻昇順を前提にしている)
			void SortKeyframes(std::vector<UIKeyframe>& keyframes)
			{
				std::sort(keyframes.begin(), keyframes.end(),
				          [](const UIKeyframe& a, const UIKeyframe& b) { return a.time < b.time; });
			}


			// keyframes の中から時刻が一致するものを探す (許容差 1e-5f)。無ければ -1
			int FindKeyIndexAtTime(const std::vector<UIKeyframe>& keyframes, float time)
			{
				for (int i = 0; i < (int)keyframes.size(); ++i)
				{
					if (std::abs(keyframes[i].time - time) < 1e-5f) return i;
				}
				return -1;
			}


			// time にあるキーを oldTime から newTime へ動かす。無ければ何もせず false
			bool MoveKeyAtTime(UIAnimationTrack& track, float oldTime, float newTime)
			{
				const int idx = FindKeyIndexAtTime(track.keyframes, oldTime);
				if (idx < 0) return false;
				track.keyframes[idx].time = newTime;
				return true;
			}


			// time にキーを追加する (既存の重複チェックはしない。従来の Add Key 系と同じ)。
			// 値は Sample(time)。Track にキーが 1 本も無ければ現在値 (ReadFrom) を使う (設計書 §15.2)
			void AddKeyAtTime(UIAnimationTrack& track, UIObject* obj, float time)
			{
				const float value = !track.keyframes.empty() ? track.Sample(time)
				                                              : (obj ? track.ReadFrom(obj) : 0.f);
				track.keyframes.push_back(UIKeyframe{ time, value, EaseType::Linear });
				SortKeyframes(track.keyframes);
			}


			// オートキーの録画対象プロパティ。Rec を ON にした瞬間に、この全部の現在値を
			// 録画基準値として控える。TextCharCount は Properties タブに編集欄が無いので入れない
			static constexpr UIAnimatedProperty RECORDABLE_PROPERTIES[] =
			{
				UIAnimatedProperty::PositionX,
				UIAnimatedProperty::PositionY,
				UIAnimatedProperty::PositionZ,
				UIAnimatedProperty::SizeDeltaX,
				UIAnimatedProperty::SizeDeltaY,
				UIAnimatedProperty::Rotation,
				UIAnimatedProperty::ScaleX,
				UIAnimatedProperty::ScaleY,
				UIAnimatedProperty::Active,
				UIAnimatedProperty::ColorR,
				UIAnimatedProperty::ColorG,
				UIAnimatedProperty::ColorB,
				UIAnimatedProperty::ColorA,
				UIAnimatedProperty::FillAmount,
				UIAnimatedProperty::NineSliceBorderLeft,
				UIAnimatedProperty::NineSliceBorderRight,
				UIAnimatedProperty::NineSliceBorderTop,
				UIAnimatedProperty::NineSliceBorderBottom,
			};


			// オートキー用。time に「編集後の現在値」のキーを置く。AddKeyAtTime() と違い
			// Sample() は使わない (録画は曲線を現在の見た目へ合わせる操作なので)。
			// 同時刻にキーがあれば value だけ上書きし ease は保つ (1 回のドラッグで増えるキーは 1 本)
			void SetKeyAtTime(UIAnimationTrack& track, UIObject* obj, float time)
			{
				const float value = obj ? track.ReadFrom(obj) : 0.f;

				const int idx = FindKeyIndexAtTime(track.keyframes, time);
				if (idx >= 0)
				{
					track.keyframes[idx].value = value;
					return;
				}

				track.keyframes.push_back(UIKeyframe{ time, value, EaseType::Linear });
				SortKeyframes(track.keyframes);
			}


			// Row の両 Track (単独行なら片方) に time のキーを追加する (設計書 §15.2)
			void AddKeyToRow(UIAnimationClip& clip, UIObject* obj, const TimelineRow& row, float time)
			{
				for (const int idx : { row.trackIdx[0], row.trackIdx[1] })
				{
					if (idx < 0 || idx >= (int)clip.tracks.size()) continue;
					AddKeyAtTime(clip.tracks[idx], obj, time);
				}
			}


			// Row が time に持つキーを両 Track (単独行なら片方) から消す。1 本でも消せたら true
			bool EraseSelectedKey(UIAnimationClip& clip, const TimelineRow& row, float time)
			{
				bool erased = false;
				for (const int idx : { row.trackIdx[0], row.trackIdx[1] })
				{
					if (idx < 0 || idx >= (int)clip.tracks.size()) continue;
					auto& keyframes = clip.tracks[idx].keyframes;
					const int ki = FindKeyIndexAtTime(keyframes, time);
					if (ki >= 0)
					{
						keyframes.erase(keyframes.begin() + ki);
						erased = true;
					}
				}
				return erased;
			}
		} // namespace


		// "+ Preset" が生成するプリセットの一式 (設計書 §14)。生成後は普通の Clip として
		// Timeline で自由に直せる。数値はここに定数として集約する
		namespace
		{
			enum class PresetKind
			{
				FadeIn = 0,
				FadeOut,
				PopIn,
				SlideIn,
				Blink,
				Shake,
				Count,
			};

			static const char* PRESET_LABELS[static_cast<int>(PresetKind::Count)] =
			{
				"FadeIn", "FadeOut", "PopIn", "SlideIn", "Blink", "Shake",
			};

			/** プリセットの既定値 (設計書 §14 の表) */
			static constexpr float PRESET_FADE_DURATION    = 0.30f;
			static constexpr float PRESET_POPIN_DURATION   = 0.25f;
			static constexpr float PRESET_SLIDEIN_DURATION = 0.30f;
			static constexpr float PRESET_BLINK_DURATION   = 0.60f;
			static constexpr float PRESET_SHAKE_DURATION   = 0.30f;
			static constexpr float PRESET_BLINK_MID_TIME   = 0.30f; // Blink の中間キーフレーム時刻
			static constexpr float PRESET_SLIDEIN_OFFSET   = 200.f; // px
			static constexpr float PRESET_SHAKE_OFFSET     = 8.f;   // px
			static constexpr float PRESET_POPIN_SCALE      = 0.8f;  // ×0.8
			static constexpr float PRESET_BLINK_MIN_SCALE  = 0.3f;  // ×0.3


			// clips 内で name と重複しない名前を作る (非重複ならそのまま。重複したら name1, name2 ...)
			std::string UniqueClipName(const std::vector<UIAnimationClip>& clips, const std::string& name)
			{
				auto nameExists = [&clips](const std::string& n)
					{
						for (const auto& c : clips) { if (c.name == n) return true; }
						return false;
					};

				std::string result = name;
				int idx = 1;
				while (nameExists(result))
					result = name + std::to_string(idx++);
				return result;
			}


			// そのプロパティの現在値を読む。ColorA は描画コンポーネントが無いと ReadFrom が
			// 0 を返すため、その場合は 1.0 とみなす (設計書 §14)
			float ReadPresetCurrentValue(const UIObject* obj, UIAnimatedProperty property)
			{
				if (property == UIAnimatedProperty::ColorA && (!obj || !obj->HasRenderComponent()))
					return 1.f;

				UIAnimationTrack t;
				t.property = property;
				return t.ReadFrom(obj);
			}


			// プリセットに対応する "Play when" (UIAnimationEditor::ApplyPlayWhenSelection の option。設計書 §10.3)
			int PresetPlayWhenOption(PresetKind kind)
			{
				switch (kind)
				{
				case PresetKind::FadeIn:  return PlayWhen_Enter;
				case PresetKind::FadeOut: return PlayWhen_Exit;
				case PresetKind::PopIn:   return PlayWhen_Enter;
				case PresetKind::SlideIn: return PlayWhen_Enter;
				case PresetKind::Blink:   return PlayWhen_Focused;
				case PresetKind::Shake:   return PlayWhen_Click;
				default:                  return PlayWhen_Manual;
				}
			}


			// §14 の表どおりに Track / Keyframe を組み立てる。Play when (condition / group) は
			// 呼び出し側が ApplyPlayWhenSelection() で設定するので、ここでは Manual のまま触らない
			UIAnimationClip MakePresetClip(PresetKind kind, const UIObject* obj, std::string name)
			{
				auto addKey = [](UIAnimationTrack& track, float time, float value, EaseType ease)
					{
						track.keyframes.push_back(UIKeyframe{ time, value, ease });
					};

				UIAnimationClip clip;
				clip.name = std::move(name);

				switch (kind)
				{
				case PresetKind::FadeIn:
				{
					clip.duration = PRESET_FADE_DURATION;
					clip.finish   = UIAnimationFinishMode::Hold;
					const float cur = ReadPresetCurrentValue(obj, UIAnimatedProperty::ColorA);

					UIAnimationTrack colorA;
					colorA.property = UIAnimatedProperty::ColorA;
					addKey(colorA, 0.f,           0.f, EaseType::EaseOut);
					addKey(colorA, clip.duration, cur,  EaseType::EaseOut);
					clip.tracks.push_back(std::move(colorA));
					break;
				}

				case PresetKind::FadeOut:
				{
					clip.duration = PRESET_FADE_DURATION;
					clip.finish   = UIAnimationFinishMode::Hold;
					const float cur = ReadPresetCurrentValue(obj, UIAnimatedProperty::ColorA);

					UIAnimationTrack colorA;
					colorA.property = UIAnimatedProperty::ColorA;
					addKey(colorA, 0.f,           cur, EaseType::EaseIn);
					addKey(colorA, clip.duration, 0.f, EaseType::EaseIn);
					clip.tracks.push_back(std::move(colorA));
					break;
				}

				case PresetKind::PopIn:
				{
					clip.duration = PRESET_POPIN_DURATION;
					clip.finish   = UIAnimationFinishMode::Hold;
					const float curScaleX = ReadPresetCurrentValue(obj, UIAnimatedProperty::ScaleX);
					const float curScaleY = ReadPresetCurrentValue(obj, UIAnimatedProperty::ScaleY);
					const float curColorA = ReadPresetCurrentValue(obj, UIAnimatedProperty::ColorA);

					UIAnimationTrack scaleX;
					scaleX.property = UIAnimatedProperty::ScaleX;
					addKey(scaleX, 0.f,           curScaleX * PRESET_POPIN_SCALE, EaseType::EaseOut);
					addKey(scaleX, clip.duration, curScaleX,                      EaseType::EaseOut);
					clip.tracks.push_back(std::move(scaleX));

					UIAnimationTrack scaleY;
					scaleY.property = UIAnimatedProperty::ScaleY;
					addKey(scaleY, 0.f,           curScaleY * PRESET_POPIN_SCALE, EaseType::EaseOut);
					addKey(scaleY, clip.duration, curScaleY,                      EaseType::EaseOut);
					clip.tracks.push_back(std::move(scaleY));

					UIAnimationTrack colorA;
					colorA.property = UIAnimatedProperty::ColorA;
					addKey(colorA, 0.f,           0.f,       EaseType::Linear);
					addKey(colorA, clip.duration, curColorA, EaseType::Linear);
					clip.tracks.push_back(std::move(colorA));
					break;
				}

				case PresetKind::SlideIn:
				{
					clip.duration = PRESET_SLIDEIN_DURATION;
					clip.finish   = UIAnimationFinishMode::Hold;
					const float cur = ReadPresetCurrentValue(obj, UIAnimatedProperty::PositionX);

					UIAnimationTrack posX;
					posX.property = UIAnimatedProperty::PositionX;
					addKey(posX, 0.f,           cur - PRESET_SLIDEIN_OFFSET, EaseType::EaseOut);
					addKey(posX, clip.duration, cur,                        EaseType::EaseOut);
					clip.tracks.push_back(std::move(posX));
					break;
				}

				case PresetKind::Blink:
				{
					clip.duration = PRESET_BLINK_DURATION;
					clip.finish   = UIAnimationFinishMode::Restore;
					clip.loopFrom = 0.f; // Loop: on (0 から)
					const float cur = ReadPresetCurrentValue(obj, UIAnimatedProperty::ColorA);

					UIAnimationTrack colorA;
					colorA.property = UIAnimatedProperty::ColorA;
					addKey(colorA, 0.f,                   cur,                          EaseType::EaseInOut);
					addKey(colorA, PRESET_BLINK_MID_TIME, cur * PRESET_BLINK_MIN_SCALE,  EaseType::EaseInOut);
					addKey(colorA, clip.duration,         cur,                          EaseType::EaseInOut);
					clip.tracks.push_back(std::move(colorA));
					break;
				}

				case PresetKind::Shake:
				{
					clip.duration = PRESET_SHAKE_DURATION;
					clip.finish   = UIAnimationFinishMode::Restore;
					const float cur = ReadPresetCurrentValue(obj, UIAnimatedProperty::PositionX);

					UIAnimationTrack posX;
					posX.property = UIAnimatedProperty::PositionX;
					addKey(posX, 0.00f,         cur,                       EaseType::Linear);
					addKey(posX, 0.05f,         cur + PRESET_SHAKE_OFFSET, EaseType::Linear);
					addKey(posX, 0.10f,         cur - PRESET_SHAKE_OFFSET, EaseType::Linear);
					addKey(posX, 0.15f,         cur + PRESET_SHAKE_OFFSET, EaseType::Linear);
					addKey(posX, 0.20f,         cur - PRESET_SHAKE_OFFSET, EaseType::Linear);
					addKey(posX, clip.duration, cur,                       EaseType::Linear);
					clip.tracks.push_back(std::move(posX));
					break;
				}

				default:
					break;
				}

				return clip;
			}
		} // namespace


		// ---- Animation タブ -----------------------------------------------------

		void UIAnimationEditor::DrawAnimationTab(UIObject* obj)
		{
			if (!obj) return;
			auto* anim = obj->GetComponent<UIAnimationComponent>();
			if (!anim) return;

			// 選択 Clip が範囲外なら解除 (Clip 削除直後など)
			if (selClipIdx_ >= (int)anim->GetClips().size())
			{
				selClipIdx_     = -1;
				prevSelClipIdx_ = -1;
			}

			const auto& clips = anim->GetClips();

			// 検証は毎フレーム掛ける。上の一覧の赤字と下のエラー表示の両方に使う
			std::vector<UIAnimationSerializer::ValidationError> errors;
			UIAnimationSerializer::ValidateDetailed(clips, errors);

			auto hasError = [&errors](const int clipIdx)
				{
					for (const auto& e : errors)
					{
						if (static_cast<int>(e.clipIndex) == clipIdx) return true;
					}
					return false;
				};

			// ---- Clip 一覧 ----
			ImGui::Text("Clips");
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Clip"))
			{
				UIAnimationClip nc;
				nc.name     = "NewClip";
				nc.duration = 1.f;

				auto nameExists = [&clips](const std::string& n)
					{
						for (const auto& c : clips) { if (c.name == n) return true; }
						return false;
					};
				int idx = 1;
				while (nameExists(nc.name))
					nc.name = "NewClip" + std::to_string(idx++);

				const size_t newIdx = anim->AddClip(std::move(nc));
				// AddClip は group を引き直さない (groupName 空 = 未解決のまま)。
				// SetClipGroup("") で group = aqHash32(name) を確定させる (§4.3)
				anim->SetClipGroup(newIdx, "");
				selClipIdx_ = static_cast<int>(newIdx);
				selRowIdx_  = -1;
				selKeyTime_ = -1.f;
				UIEditorSession::Get().dirty = true;
			}
			ImGui::SameLine();
			{
				const bool canRemove = selClipIdx_ >= 0 && selClipIdx_ < (int)clips.size();
				if (!canRemove) ImGui::BeginDisabled();
				if (ImGui::SmallButton("- Clip"))
				{
					if (hasSnapshot_) { RestoreSnapshot(obj); hasSnapshot_ = false; }
					anim->RemoveClip(static_cast<size_t>(selClipIdx_));
					selClipIdx_     = -1;
					prevSelClipIdx_ = -1;
					selRowIdx_      = -1;
					selKeyTime_     = -1.f;
					UIEditorSession::Get().dirty = true;
				}
				if (!canRemove) ImGui::EndDisabled();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Preset"))
				ImGui::OpenPopup("preset_menu");
			if (ImGui::BeginPopup("preset_menu"))
			{
				for (int i = 0; i < static_cast<int>(PresetKind::Count); ++i)
				{
					if (ImGui::MenuItem(PRESET_LABELS[i]))
					{
						const auto kind = static_cast<PresetKind>(i);
						UIAnimationClip nc = MakePresetClip(kind, obj, UniqueClipName(clips, PRESET_LABELS[i]));
						const size_t newIdx = anim->AddClip(std::move(nc));

						// Play when (condition / group) は ApplyPlayWhenSelection() を通す (設計書 §14)
						auto& newClip = anim->EditClip(newIdx);
						ApplyPlayWhenSelection(anim, static_cast<int>(newIdx), newClip, PresetPlayWhenOption(kind));

						selClipIdx_ = static_cast<int>(newIdx);
						selRowIdx_  = -1;
						selKeyTime_ = -1.f;
						UIEditorSession::Get().dirty = true;
					}
				}
				ImGui::EndPopup();
			}

			ImGui::Separator();

			for (int i = 0; i < (int)clips.size(); ++i)
			{
				ImGui::PushID(i);
				const bool sel = (i == selClipIdx_);
				const bool err = hasError(i);
				if (err) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
				if (ImGui::Selectable(clips[i].name.c_str(), sel))
				{
					if (selClipIdx_ != i)
					{
						selClipIdx_ = i;
						selRowIdx_  = -1;
						selKeyTime_ = -1.f;
						scrubTime_  = 0.f;
						isPlaying_  = false;
						if (hasSnapshot_) { RestoreSnapshot(obj); hasSnapshot_ = false; }
					}
				}
				if (err) ImGui::PopStyleColor();
				ImGui::PopID();
			}

			if (selClipIdx_ < 0 || selClipIdx_ >= (int)clips.size())
			{
				ImGui::Separator();
				if (!errors.empty())
					ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%zu error(s) in other clips", errors.size());
				return;
			}

			auto& clip = anim->EditClip(static_cast<size_t>(selClipIdx_));

			// 選択クリップが変わったときだけバッファを同期する
			if (selClipIdx_ != prevSelClipIdx_)
			{
				SyncClipBuffers(clip);
				prevSelClipIdx_ = selClipIdx_;
			}

			ImGui::Separator();

			// Name
			ImGui::SetNextItemWidth(160.f);
			if (ImGui::InputText("Name", clipNameBuf_, sizeof(clipNameBuf_), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				const std::string newName(clipNameBuf_);
				if (!newName.empty())
				{
					clip.name = newName;
					// groupName が空のクリップは group = aqHash32(name) なので、改名で group も引き直す (§4.3)
					if (clip.groupName.empty())
						anim->SetClipGroup(static_cast<size_t>(selClipIdx_), "");
					UIEditorSession::Get().dirty = true;
				}
				else
				{
					// 空は拒否してバッファを元に戻す
					std::snprintf(clipNameBuf_, sizeof(clipNameBuf_), "%s", clip.name.c_str());
				}
			}

			// Play when
			{
				const int pwIdx = ComputePlayWhenIndex(clip);
				ImGui::SetNextItemWidth(140.f);
				if (ImGui::BeginCombo("Play when", PLAY_WHEN_LABELS[pwIdx]))
				{
					for (int k = 0; k < PlayWhen_Count; ++k)
					{
						ImGui::PushID(k);
						const bool isExitOption = (k == PlayWhen_Exit);
						const bool disabled     = isExitOption && !kExitTransitionReady;
						const bool selected     = (k == pwIdx);

						if (disabled) ImGui::BeginDisabled();
						if (ImGui::Selectable(PLAY_WHEN_LABELS[k], selected))
						{
							ApplyPlayWhenSelection(anim, selClipIdx_, clip, k);
							SyncClipBuffers(clip);
							if (k != PlayWhen_Advanced)
								UIEditorSession::Get().dirty = true;
						}
						if (disabled) ImGui::EndDisabled();
						if (disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
							ImGui::SetTooltip("(available after screen transition support)");
						if (selected) ImGui::SetItemDefaultFocus();
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
			}

			// Group (Play when が Manual のときだけ)。Enter / Exit も内部は Manual + 予約 group なので、
			// condition ではなく Play when の表示値で判定する。condition / conditionParam は Advanced へ
			if (ComputePlayWhenIndex(clip) == PlayWhen_Manual)
			{
				ImGui::SetNextItemWidth(120.f);
				if (ImGui::InputText("Group", clipGroupBuf_, sizeof(clipGroupBuf_), ImGuiInputTextFlags_EnterReturnsTrue))
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

			// Duration
			ImGui::SetNextItemWidth(100.f);
			ImGui::DragFloat("Duration", &clip.duration, 0.01f, 0.01f, 60.f, "%.2f sec");
			if (ImGui::IsItemEdited())
				UIEditorSession::Get().dirty = true;
			clip.duration = std::clamp(clip.duration, 0.01f, 60.f);

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
				ImGui::SetNextItemWidth(80.f);
				ImGui::DragFloat("LoopFrom", &clip.loopFrom, 0.01f, 0.f, clip.duration, "%.2f");
				if (ImGui::IsItemEdited())
					UIEditorSession::Get().dirty = true;
				clip.loopFrom = std::clamp(clip.loopFrom, 0.f, clip.duration);
			}

			// After finish
			{
				static const char* FINISH_LABELS[] = { "Keep", "Return" };
				int finishIdx = static_cast<int>(clip.finish);
				ImGui::SetNextItemWidth(100.f);
				if (ImGui::Combo("After finish", &finishIdx, FINISH_LABELS, 2))
				{
					clip.finish = static_cast<UIAnimationFinishMode>(finishIdx);
					UIEditorSession::Get().dirty = true;
				}
			}

			// Advanced (既定で閉じる。condition / conditionParam / group の生値はここだけ)
			if (wantOpenAdvanced_)
			{
				ImGui::SetNextItemOpen(true);
				wantOpenAdvanced_ = false;
			}
			if (ImGui::CollapsingHeader("Advanced"))
			{
				static const char* COND_LABELS[] = { "Manual", "Bool", "Trigger" };
				int condIdx = static_cast<int>(clip.condition);
				ImGui::SetNextItemWidth(100.f);
				if (ImGui::Combo("Condition", &condIdx, COND_LABELS, 3))
				{
					anim->SetClipCondition(static_cast<size_t>(selClipIdx_),
					                       static_cast<UIClipCondition>(condIdx),
					                       aqHash32(clipParamBuf_), clipParamBuf_);
					UIEditorSession::Get().dirty = true;
				}

				ImGui::SetNextItemWidth(140.f);
				if (ImGui::InputText("Condition param", clipParamBuf_, sizeof(clipParamBuf_), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					anim->SetClipCondition(static_cast<size_t>(selClipIdx_), clip.condition,
					                       aqHash32(clipParamBuf_), clipParamBuf_);
					UIEditorSession::Get().dirty = true;
				}

				ImGui::SetNextItemWidth(140.f);
				if (ImGui::InputText("Group##adv", clipGroupBuf_, sizeof(clipGroupBuf_), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					anim->SetClipGroup(static_cast<size_t>(selClipIdx_), clipGroupBuf_);
					UIEditorSession::Get().dirty = true;
				}

				ImGui::TextDisabled("group = 0x%08X  param = 0x%08X", clip.group, clip.conditionParam);
			}

			ImGui::Separator();

			// ---- 検証エラー: 選択クリップは全文赤字、他クリップは件数だけ ----
			int otherCount = 0;
			for (const auto& e : errors)
			{
				if (static_cast<int>(e.clipIndex) == selClipIdx_)
					ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", e.message.c_str());
				else
					++otherCount;
			}
			if (otherCount > 0)
				ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%d error(s) in other clips", otherCount);
		}


		// 選択クリップの Name / Param / Group バッファをクリップの現在値へ同期する
		void UIAnimationEditor::SyncClipBuffers(const UIAnimationClip& clip)
		{
			std::snprintf(clipNameBuf_,  sizeof(clipNameBuf_),  "%s", clip.name.c_str());
			std::snprintf(clipParamBuf_, sizeof(clipParamBuf_), "%s", clip.conditionParamName.c_str());
			std::snprintf(clipGroupBuf_, sizeof(clipGroupBuf_), "%s", clip.groupName.c_str());
		}


		// "Play when" Combo の選択値を内部設定 (condition / conditionParam / group) へ反映する (設計書 §10.3)
		void UIAnimationEditor::ApplyPlayWhenSelection(
			UIAnimationComponent* anim, const int clipIndex, UIAnimationClip& clip, const int option)
		{
			const auto idx = static_cast<size_t>(clipIndex);

			switch (option)
			{
			case PlayWhen_Enter:
				anim->SetClipCondition(idx, UIClipCondition::Manual, 0u, "");
				anim->SetClipGroup(idx, "Enter");
				break;

			case PlayWhen_Exit:
				anim->SetClipCondition(idx, UIClipCondition::Manual, 0u, "");
				anim->SetClipGroup(idx, "Exit");
				break;

			case PlayWhen_Hover:
				anim->SetClipCondition(idx, UIClipCondition::Bool, kUIAnimCondHover, "Hover");
				break;

			case PlayWhen_Pressed:
				anim->SetClipCondition(idx, UIClipCondition::Bool, kUIAnimCondPressed, "Pressed");
				break;

			case PlayWhen_Focused:
				anim->SetClipCondition(idx, UIClipCondition::Bool, kUIAnimCondFocused, "Focused");
				break;

			case PlayWhen_Click:
				anim->SetClipCondition(idx, UIClipCondition::Trigger, kUIAnimTriggerClick, "Click");
				break;

			case PlayWhen_Manual:
				anim->SetClipCondition(idx, UIClipCondition::Manual, 0u, "");
				// Enter / Exit から戻ってきた場合だけ group をクリアする。それ以外は現状維持
				if (clip.groupName == "Enter" || clip.groupName == "Exit")
					anim->SetClipGroup(idx, "");
				break;

			case PlayWhen_Advanced:
				// 内部設定は変えず、Advanced セクションを開かせるだけ
				wantOpenAdvanced_ = true;
				break;

			default:
				break;
			}
		}


		// ---- Timeline パネル (下部) ---------------------------------------------

		void UIAnimationEditor::DrawTimelinePanel(UIObject* obj, float dt)
		{
			if (!obj) return;
			auto* anim = obj->GetComponent<UIAnimationComponent>();
			if (!anim) return;

			// 選択 Clip が範囲外なら解除
			if (selClipIdx_ >= (int)anim->GetClips().size())
			{
				selClipIdx_     = -1;
				prevSelClipIdx_ = -1;
			}

			const float leftW  = 260.f;
			const float totalH = ImGui::GetContentRegionAvail().y - BOTTOM_PANE_H; // 下部パネル分を確保

			ImGui::BeginChild("##tlLeft", ImVec2(leftW, totalH), true);
			DrawTimelineClipList(obj, anim);
			ImGui::EndChild();

			ImGui::SameLine();

			ImGui::BeginChild("##tlRight", ImVec2(0, totalH), true);
			if (selClipIdx_ >= 0)
			{
				auto& clip = anim->EditClip(static_cast<size_t>(selClipIdx_));
				DrawTimeline(obj, clip);
			}
			else
			{
				ImGui::TextDisabled("Select a clip");
			}
			ImGui::EndChild();

			ImGui::Separator();

			// 下部: キーフレーム Inspector + Preview Controls
			ImGui::BeginChild("##tlBottom", ImVec2(0, 0));
			{
				if (selClipIdx_ >= 0)
				{
					auto& clip = anim->EditClip(static_cast<size_t>(selClipIdx_));

					ImGui::BeginChild("##kfinspector", ImVec2(300, 0), false);
					DrawKeyframeInspector(clip);
					ImGui::EndChild();

					ImGui::SameLine();

					ImGui::BeginChild("##preview", ImVec2(0, 0), false);
					DrawPreviewControls(obj, anim, clip, dt);
					ImGui::EndChild();
				}
			}
			ImGui::EndChild();
		}


		// UI Editor が下部 Timeline ペインの高さを決めるために呼ぶ。
		// Track 一覧と Row の本数で伸びる分に、ヘッダ・ルーラ・下部パネルの固定分を足す
		float UIAnimationEditor::ComputeTimelineHeight(UIObject* obj) const
		{
			const ImGuiStyle& style = ImGui::GetStyle();

			// Clip 未選択なら右ペインは "Select a clip" だけなので可変分は 0
			float contentH = 0.f;

			if (obj)
			{
				if (const auto* anim = obj->GetComponent<UIAnimationComponent>())
				{
					const auto& clips = anim->GetClips();
					if (selClipIdx_ >= 0 && selClipIdx_ < (int)clips.size())
					{
						const auto& clip      = clips[static_cast<size_t>(selClipIdx_)];
						const int   trackRows = (int)clip.tracks.size();
						const int   viewRows  = (int)BuildTimelineRows(clip).size();

						contentH = ImGui::GetFrameHeightWithSpacing()             // "Tracks" + [+ Track]
						         + trackRows * ImGui::GetFrameHeightWithSpacing() // Track 一覧
						         + style.ItemSpacing.y * 2.f                      // Separator
						         + ImGui::GetTextLineHeightWithSpacing()          // 操作ヒント
						         + RULER_H + viewRows * ROW_H + 10.f;             // ルーラ + Row
					}
				}
			}

			// 上ペインの枠 (親と子の WindowPadding) と Separator、下部パネルの固定分
			const float total = contentH + style.WindowPadding.y * 4.f
			                  + style.ItemSpacing.y * 2.f + BOTTOM_PANE_H;

			return std::clamp(total, TIMELINE_MIN_H, TIMELINE_MAX_H);
		}


		// ---- Selection ----------------------------------------------------------

		// UI Editor の Hierarchy 選択が変わった時の後始末。
		// 旧オブジェクトのプレビュースナップショットを戻し、Clip / Keyframe 選択を捨てる。
		void UIAnimationEditor::OnTargetChanged(UIObject* prevObj)
		{
			selClipIdx_     = -1;
			prevSelClipIdx_ = -1;
			selRowIdx_      = -1;
			selKeyTime_     = -1.f;
			wantOpenAdvanced_ = false;
			if (hasSnapshot_)
			{
				if (prevObj)
					RestoreSnapshot(prevObj);
				hasSnapshot_ = false;
			}
			isPlaying_ = false;

			// 録画は選択オブジェクトに紐づくので落とす
			isRecording_ = false;
			recBaseline_.clear();
		}


		// Reload 直前。オブジェクトはこの直後に破棄されるので触らず、選択とプレビュー状態だけを捨てる
		void UIAnimationEditor::Reset()
		{
			selClipIdx_       = -1;
			prevSelClipIdx_   = -1;
			selRowIdx_        = -1;
			selKeyTime_       = -1.f;
			wantOpenAdvanced_ = false;

			isDraggingKf_ = false;
			dragRowIdx_   = -1;
			dragKeyTime_  = -1.f;

			scrubTime_   = 0.f;
			isPlaying_   = false;
			hasSnapshot_ = false;
			snapshot_.clear();

			isRecording_ = false;
			recBaseline_.clear();
		}


		// root 以下の全 UIAnimationComponent を検証する (Save 前のチェック用)
		bool UIAnimationEditor::ValidateTree(const UIObject* root, std::vector<std::string>& errors)
		{
			if (!root) return true;

			bool ok = true;

			if (auto* anim = root->GetComponent<UIAnimationComponent>())
			{
				std::vector<UIAnimationSerializer::ValidationError> clipErrors;
				if (!UIAnimationSerializer::ValidateDetailed(anim->GetClips(), clipErrors))
				{
					ok = false;
					for (const auto& e : clipErrors)
						errors.push_back(std::string(root->GetName()) + ": " + e.message);
				}
			}

			for (const auto* child : root->GetChildren())
			{
				if (!ValidateTree(child, errors))
					ok = false;
			}

			return ok;
		}


		// ---- Timeline: 左パネル (Clip 一覧) --------------------------------------

		// Manual は groupName (空なら name) ごと、Bool / Trigger は Play when の見出しごとにまとめる。
		// データは平ら (clip の並びは変えない)。見た目だけの 2 段
		void UIAnimationEditor::DrawTimelineClipList(UIObject* obj, UIAnimationComponent* anim)
		{
			const auto& clips = anim->GetClips();

			struct Group
			{
				std::string      header;
				std::vector<int> clipIndices;
			};
			std::vector<Group> groups;

			for (int i = 0; i < (int)clips.size(); ++i)
			{
				const std::string header = ComputeGroupHeader(clips[i]);

				Group* found = nullptr;
				for (auto& g : groups)
				{
					if (g.header == header) { found = &g; break; }
				}
				if (!found)
				{
					groups.push_back(Group{ header, {} });
					found = &groups.back();
				}
				found->clipIndices.push_back(i);
			}

			for (const auto& group : groups)
			{
				ImGui::TextDisabled("%s", group.header.empty() ? "(unnamed)" : group.header.c_str());

				for (const int i : group.clipIndices)
				{
					const auto& clip = clips[i];
					ImGui::PushID(i);

					const bool sel = (i == selClipIdx_);
					if (ImGui::Selectable(clip.name.c_str(), sel))
					{
						if (selClipIdx_ != i)
						{
							selClipIdx_     = i;
							selRowIdx_  = -1;
							selKeyTime_ = -1.f;
							scrubTime_      = 0.f;
							isPlaying_      = false;
							if (hasSnapshot_) { RestoreSnapshot(obj); hasSnapshot_ = false; }
						}
					}

					// 要約: Play when / Duration / Loop
					ImGui::Indent();
					ImGui::TextDisabled("%s  %.2fs%s",
					                     PLAY_WHEN_LABELS[ComputePlayWhenIndex(clip)],
					                     clip.duration,
					                     (clip.loopFrom >= 0.f ? "  Loop" : ""));
					ImGui::Unindent();

					ImGui::PopID();
				}
			}
		}


		void UIAnimationEditor::DrawTrackList(UIAnimationClip& clip)
		{
			ImGui::Text("Tracks");
			ImGui::SameLine();
			if (ImGui::SmallButton("+ Track"))
				ImGui::OpenPopup("track_add_menu");

			if (ImGui::BeginPopup("track_add_menu"))
			{
				if (ImGui::MenuItem("New Track"))
				{
					UIAnimationTrack track;
					track.property = UIAnimatedProperty::PositionX;
					clip.tracks.push_back(std::move(track));
					UIEditorSession::Get().dirty = true;
				}

				ImGui::Separator();

				// X/Y が揃った組を一度に足す (設計書 §15.1)。無い方の Track だけ追加する
				struct VectorAddItem { const char* label; UIAnimatedProperty x; UIAnimatedProperty y; };
				static constexpr VectorAddItem VECTOR_ADD_ITEMS[] =
				{
					{ "Position (X,Y)",  UIAnimatedProperty::PositionX,  UIAnimatedProperty::PositionY },
					{ "Scale (X,Y)",     UIAnimatedProperty::ScaleX,     UIAnimatedProperty::ScaleY },
					{ "SizeDelta (X,Y)", UIAnimatedProperty::SizeDeltaX, UIAnimatedProperty::SizeDeltaY },
				};

				auto hasProperty = [&clip](UIAnimatedProperty prop)
					{
						for (const auto& t : clip.tracks) { if (t.property == prop) return true; }
						return false;
					};

				for (const auto& item : VECTOR_ADD_ITEMS)
				{
					const bool hasX = hasProperty(item.x);
					const bool hasY = hasProperty(item.y);
					const bool bothPresent = hasX && hasY;

					if (bothPresent) ImGui::BeginDisabled();
					if (ImGui::MenuItem(item.label))
					{
						if (!hasX) { UIAnimationTrack t; t.property = item.x; clip.tracks.push_back(std::move(t)); }
						if (!hasY) { UIAnimationTrack t; t.property = item.y; clip.tracks.push_back(std::move(t)); }
						UIEditorSession::Get().dirty = true;
					}
					if (bothPresent) ImGui::EndDisabled();
				}

				ImGui::EndPopup();
			}

			// Track 単位の一覧。選択は所属する Row (単独なら自分だけの Row、組の一部なら
			// X/Y をまとめた Row) を選ぶ (設計書 §15.1 / §15.2)
			const auto rows = BuildTimelineRows(clip);
			auto findOwnerRow = [&rows](int trackIdx)
				{
					for (int r = 0; r < (int)rows.size(); ++r)
					{
						if (rows[r].trackIdx[0] == trackIdx || rows[r].trackIdx[1] == trackIdx)
							return r;
					}
					return -1;
				};

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

				const int  ownerRow = findOwnerRow(ti);
				const bool trackSel = (ownerRow >= 0 && ownerRow == selRowIdx_);
				char trackLabel[64];
				std::snprintf(trackLabel, sizeof(trackLabel), "%s",
				              UIAnimationSerializer::PropertyToStr(track.property));

				if (ImGui::Selectable(trackLabel, trackSel, ImGuiSelectableFlags_AllowOverlap))
				{
					selRowIdx_  = ownerRow;
					selKeyTime_ = -1.f;
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
					// 削除で Row 構成が変わるため、安全のため選択は解除する
					selRowIdx_  = -1;
					selKeyTime_ = -1.f;
					UIEditorSession::Get().dirty = true;
					ImGui::PopID();
					break; // 残りは次フレームで描く (erase 後に ++ti すると 1 本飛ばすため continue にしない)
				}

				ImGui::PopID();
			}
		}


		// ---- Timeline: 右パネル ---------------------------------------------------

		void UIAnimationEditor::DrawTimeline(UIObject* obj, UIAnimationClip& clip)
		{
			auto* anim = obj->GetComponent<UIAnimationComponent>();

			// Track の追加 / 削除 / プロパティ選択 (Timeline の一部として扱う)
			DrawTrackList(clip);
			ImGui::Separator();

			const float totalDur = clip.duration;
			const float totalW   = totalDur * zoomPxPerSec_;

			// 表示行は毎フレーム clip.tracks から組み立てる。X/Y が揃った
			// Position / Scale / SizeDelta は 1 行にまとめる (設計書 §15.1)
			const auto rows      = BuildTimelineRows(clip);
			const int  totalRows = (int)rows.size();

			const float contentW = LABEL_W + totalW + 20.f;
			const float contentH = RULER_H + totalRows * ROW_H + 10.f;

			// 操作ヒント
			ImGui::TextDisabled("Right click: add/remove key  |  Ctrl+Click: add key  |  Delete: remove  |  Drag: move  |  Wheel: zoom");

			// 横スクロール可能な子ウィンドウ
			// 高さは内容分を確保する (0 = 残り全部だと Track 一覧に押されて行が見えなくなる)。
			// 親の ##tlRight 側がスクロールする
			ImGui::BeginChild("##tlscroll", ImVec2(0, contentH + 20.f), false,
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

			// Row 描画 (組の行は X/Y 2 本の Track をまとめて 1 行として描く。設計書 §15.1)
			float rowY = tlY + RULER_H;
			for (int ri = 0; ri < totalRows; ++ri)
			{
				const auto& row = rows[ri];
				DrawTrackRow(dl, anim, clip, ri, row.label, row.trackIdx[0], row.trackIdx[1],
				             tlX, rowY, ROW_H, totalDur);
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

			// Ctrl+クリックで選択 Row にキーフレーム追加 (組なら両 Track に同時刻で。設計書 §15.2)
			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) &&
			    ImGui::GetIO().KeyCtrl &&
			    selRowIdx_ >= 0 && selRowIdx_ < totalRows)
			{
				const float t = std::clamp((mouse.x - tlX) / zoomPxPerSec_, 0.f, totalDur);
				AddKeyToRow(clip, obj, rows[selRowIdx_], t);
				selKeyTime_ = t;
				UIEditorSession::Get().dirty = true;
			}

			// キーフレームドラッグ処理 (クリック開始位置がキーフレーム上のときのみ動く)。
			// Row index + 掴んだ時刻で持ち、組なら両 Track のその時刻のキーを一緒に動かす
			if (dragRowIdx_ >= 0 && dragRowIdx_ < totalRows)
			{
				const auto& dragRow = rows[dragRowIdx_];
				if (ImGui::IsMouseDragging(0, 2.f))
				{
					isDraggingKf_ = true;
					const float dx      = ImGui::GetIO().MouseDelta.x;
					const float newTime = std::clamp(dragKeyTime_ + dx / zoomPxPerSec_, 0.f, totalDur);

					bool moved = false;
					if (dragRow.trackIdx[0] >= 0)
						moved |= MoveKeyAtTime(clip.tracks[dragRow.trackIdx[0]], dragKeyTime_, newTime);
					if (dragRow.trackIdx[1] >= 0)
						moved |= MoveKeyAtTime(clip.tracks[dragRow.trackIdx[1]], dragKeyTime_, newTime);

					if (moved)
						dragKeyTime_ = newTime;
				}
				if (isDraggingKf_ && ImGui::IsMouseReleased(0))
				{
					if (dragRow.trackIdx[0] >= 0) SortKeyframes(clip.tracks[dragRow.trackIdx[0]].keyframes);
					if (dragRow.trackIdx[1] >= 0) SortKeyframes(clip.tracks[dragRow.trackIdx[1]].keyframes);
					selRowIdx_  = dragRowIdx_;
					selKeyTime_ = dragKeyTime_;
					UIEditorSession::Get().dirty = true;
				}
			}
			if (ImGui::IsMouseReleased(0))
			{
				isDraggingKf_ = false;
				dragRowIdx_   = -1;
				dragKeyTime_  = -1.f;
			}

			// Deleteキー → 選択キーフレーム削除 (組なら両方)
			if (ImGui::IsWindowHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
			    selRowIdx_ >= 0 && selRowIdx_ < totalRows && selKeyTime_ >= 0.f)
			{
				if (EraseSelectedKey(clip, rows[selRowIdx_], selKeyTime_))
				{
					selKeyTime_ = -1.f;
					UIEditorSession::Get().dirty = true;
				}
			}

			// キーフレームコンテキストメニュー (組なら両方に効く。設計書 §15.2)
			if (ImGui::BeginPopup("kf_ctx"))
			{
				if (selRowIdx_ >= 0 && selRowIdx_ < totalRows && selKeyTime_ >= 0.f)
				{
					const auto& row     = rows[selRowIdx_];
					auto&       trackX  = clip.tracks[row.trackIdx[0]];
					auto* const trackY  = (row.trackIdx[1] >= 0) ? &clip.tracks[row.trackIdx[1]] : nullptr;
					const int   xi      = FindKeyIndexAtTime(trackX.keyframes, selKeyTime_);
					const int   yi      = trackY ? FindKeyIndexAtTime(trackY->keyframes, selKeyTime_) : -1;

					if (xi >= 0 || yi >= 0)
					{
						if (trackY)
							ImGui::TextDisabled("%s  t=%.3f", row.label, selKeyTime_);
						else
							ImGui::TextDisabled("%s  t=%.3f  v=%.3f", row.label, selKeyTime_, trackX.keyframes[xi].value);
						ImGui::Separator();

						static const char* EASE_MENU_LABELS[] =
							{ "Linear","EaseIn","EaseOut","EaseInOut","Bezier" };
						if (ImGui::BeginMenu("Ease"))
						{
							const EaseType curEase = (xi >= 0) ? trackX.keyframes[xi].ease : trackY->keyframes[yi].ease;
							for (int ei = 0; ei < 5; ++ei)
							{
								bool isCur = (static_cast<int>(curEase) == ei);
								if (ImGui::MenuItem(EASE_MENU_LABELS[ei], nullptr, isCur))
								{
									if (xi >= 0) trackX.keyframes[xi].ease = static_cast<EaseType>(ei);
									if (yi >= 0) trackY->keyframes[yi].ease = static_cast<EaseType>(ei);
									UIEditorSession::Get().dirty = true;
								}
							}
							ImGui::EndMenu();
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Duplicate Key"))
						{
							const float dupTime = std::min(selKeyTime_ + 0.1f, clip.duration);
							if (xi >= 0)
							{
								UIKeyframe dup = trackX.keyframes[xi];
								dup.time = dupTime;
								trackX.keyframes.push_back(dup);
								SortKeyframes(trackX.keyframes);
							}
							if (yi >= 0)
							{
								UIKeyframe dup = trackY->keyframes[yi];
								dup.time = dupTime;
								trackY->keyframes.push_back(dup);
								SortKeyframes(trackY->keyframes);
							}
							selKeyTime_ = dupTime;
							UIEditorSession::Get().dirty = true;
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Delete Key"))
						{
							if (xi >= 0) trackX.keyframes.erase(trackX.keyframes.begin() + xi);
							if (yi >= 0) trackY->keyframes.erase(trackY->keyframes.begin() + yi);
							selKeyTime_ = -1.f;
							UIEditorSession::Get().dirty = true;
						}
					}
				}
				ImGui::EndPopup();
			}

			// トラック空領域コンテキストメニュー (組なら両 Track に同時刻で追加。設計書 §15.2)
			if (ImGui::BeginPopup("track_ctx"))
			{
				if (selRowIdx_ >= 0 && selRowIdx_ < totalRows)
				{
					const auto& row = rows[selRowIdx_];
					ImGui::TextDisabled("%s  t=%.3f", row.label, ctxClickTime_);
					ImGui::Separator();
					if (ImGui::MenuItem("Add Key here"))
					{
						AddKeyToRow(clip, obj, row, ctxClickTime_);
						selKeyTime_ = ctxClickTime_;
						UIEditorSession::Get().dirty = true;
					}
					if (ImGui::MenuItem("Add Key at Scrub"))
					{
						AddKeyToRow(clip, obj, row, scrubTime_);
						selKeyTime_ = scrubTime_;
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
			ImDrawList* dl, UIAnimationComponent* anim, UIAnimationClip& clip,
			int rowIdx, const char* label, int trackIdxX, int trackIdxY,
			float ox, float rowY, float rowH, float duration)
		{
			if (trackIdxX < 0 || trackIdxX >= (int)clip.tracks.size()) return;
			auto&       trackX = clip.tracks[trackIdxX];
			auto* const trackY = (trackIdxY >= 0 && trackIdxY < (int)clip.tracks.size())
				? &clip.tracks[trackIdxY] : nullptr;

			const bool   rowSel = (rowIdx == selRowIdx_);
			const ImVec2 mouse  = ImGui::GetMousePos();

			// このプロパティの勝者がこのクリップなら先頭に "*" を出す。組の行は X / Y の
			// どちらかがこの Clip の勝者なら出す (設計書 §15.2)
			const bool isWinner = anim && selClipIdx_ >= 0 &&
			                      (anim->FindWinnerClip(trackX.property) == selClipIdx_ ||
			                       (trackY && anim->FindWinnerClip(trackY->property) == selClipIdx_));

			// ラベル列 (ox - LABEL_W .. ox) — content スクロールに乗る
			const ImU32 lblBg = rowSel ? IM_COL32(50, 80, 120, 210) : IM_COL32(35, 38, 48, 210);
			dl->AddRectFilled(ImVec2(ox - LABEL_W, rowY), ImVec2(ox, rowY + rowH), lblBg);
			// ラベルテキストは window 左端固定 (スクロールしても読める)
			char labelBuf[64];
			std::snprintf(labelBuf, sizeof(labelBuf), "%s%s", isWinner ? "* " : "", label);
			dl->AddText(ImVec2(ox - LABEL_W + 4.f, rowY + 4.f),
			            IM_COL32(200, 215, 235, 255),
			            labelBuf);

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
				selRowIdx_  = rowIdx;
				selKeyTime_ = -1.f;
			}

			// 両 Track の時刻の和集合を作る (組の行のみ Y 側も見る)。
			// 両方に在る時刻は塗りつぶし、片方だけの時刻は枠だけの菱形 (設計書 §15.2)
			struct UnionKey { float time; bool inX; bool inY; };
			std::vector<UnionKey> unionKeys;
			auto addUnion = [&unionKeys](float t, bool isX)
				{
					for (auto& u : unionKeys)
					{
						if (std::abs(u.time - t) < 1e-5f)
						{
							if (isX) u.inX = true; else u.inY = true;
							return;
						}
					}
					UnionKey u{ t, false, false };
					if (isX) u.inX = true; else u.inY = true;
					unionKeys.push_back(u);
				};
			for (const auto& kf : trackX.keyframes) addUnion(kf.time, true);
			if (trackY) { for (const auto& kf : trackY->keyframes) addUnion(kf.time, false); }

			// キーフレーム描画 & クリック
			const float cy = rowY + rowH * 0.5f;
			bool anyKfRightClicked = false;
			bool anyKfHovered      = false;
			for (const auto& uk : unionKeys)
			{
				const float kx     = ox + uk.time * zoomPxPerSec_;
				const bool  kSel   = (rowIdx == selRowIdx_ && std::abs(uk.time - selKeyTime_) < 1e-5f);
				const bool  filled = uk.inX && (!trackY || uk.inY);

				// 菱形 (ドラッグ中は半透明。片方だけの時刻は枠だけ)
				const ImU32 kCol = kSel
					? (isDraggingKf_ ? IM_COL32(255, 220, 50, 140) : IM_COL32(255, 220, 50, 255))
					: IM_COL32(120, 200, 120, 255);
				if (filled)
				{
					dl->AddQuadFilled(
						ImVec2(kx,              cy - DIAMOND_R),
						ImVec2(kx + DIAMOND_R,  cy),
						ImVec2(kx,              cy + DIAMOND_R),
						ImVec2(kx - DIAMOND_R,  cy),
						kCol);
				}
				dl->AddQuad(
					ImVec2(kx,              cy - DIAMOND_R),
					ImVec2(kx + DIAMOND_R,  cy),
					ImVec2(kx,              cy + DIAMOND_R),
					ImVec2(kx - DIAMOND_R,  cy),
					filled ? IM_COL32(255, 255, 255, 100) : kCol);

				const float kHit = DIAMOND_R + 2.f;
				const bool inKf = mouse.x >= kx - kHit && mouse.x <= kx + kHit &&
				                  mouse.y >= cy - kHit && mouse.y <= cy + kHit;

				// 左クリックで選択 & ドラッグ開始ソース記録
				if (inKf && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
				{
					selRowIdx_   = rowIdx;
					selKeyTime_  = uk.time;
					dragRowIdx_  = rowIdx;
					dragKeyTime_ = uk.time;
				}

				// 右クリック → キーフレームコンテキストメニュー
				if (inKf && ImGui::IsMouseClicked(1) && ImGui::IsWindowHovered() && !isDraggingKf_)
				{
					selRowIdx_        = rowIdx;
					selKeyTime_       = uk.time;
					anyKfRightClicked = true;
					ImGui::OpenPopup("kf_ctx");
				}

				// Tooltip (ドラッグ中は非表示)
				if (inKf && !isDraggingKf_)
				{
					anyKfHovered = true;
					ImGui::BeginTooltip();
					if (trackY)
					{
						ImGui::Text("t=%.3f", uk.time);
						if (uk.inX) ImGui::Text("X=%.3f", trackX.Sample(uk.time)); else ImGui::TextDisabled("X=-");
						if (uk.inY) ImGui::Text("Y=%.3f", trackY->Sample(uk.time)); else ImGui::TextDisabled("Y=-");
					}
					else
					{
						const int ki = FindKeyIndexAtTime(trackX.keyframes, uk.time);
						if (ki >= 0)
						{
							const auto& kf = trackX.keyframes[ki];
							ImGui::Text("t=%.3f  v=%.3f  ease=%s",
							            kf.time, kf.value,
							            UIAnimationSerializer::EaseToStr(kf.ease));
						}
					}
					ImGui::EndTooltip();
				}
			}

			// 行ホバー時のツールチップ: active / time / serial / winner (設計書 §10.4)。
			// 組の行は X / Y それぞれの勝者を出す (設計書 §15.2)。
			// キーフレーム自体のツールチップが出ているときは重ねない
			const bool inRow = mouse.y >= rowY && mouse.y < rowY + rowH &&
			                   mouse.x >= ox - LABEL_W && mouse.x <= ox + trackW;
			if (inRow && !anyKfHovered && ImGui::IsWindowHovered() && anim && selClipIdx_ >= 0)
			{
				const auto selIdx = static_cast<size_t>(selClipIdx_);
				ImGui::BeginTooltip();
				if (trackY)
				{
					ImGui::Text("active=%d time=%.2f serial=%u",
					            anim->IsClipActive(selIdx) ? 1 : 0,
					            anim->GetClipTime(selIdx),
					            anim->GetClipSerial(selIdx));
					ImGui::Text("winner X=#%d Y=#%d",
					            anim->FindWinnerClip(trackX.property),
					            anim->FindWinnerClip(trackY->property));
				}
				else
				{
					ImGui::Text("active=%d time=%.2f serial=%u winner=#%d",
					            anim->IsClipActive(selIdx) ? 1 : 0,
					            anim->GetClipTime(selIdx),
					            anim->GetClipSerial(selIdx),
					            anim->FindWinnerClip(trackX.property));
				}
				ImGui::EndTooltip();
			}

			// 空トラック領域の右クリック → キー追加コンテキストメニュー
			const bool inTrackArea = mouse.x >= ox && mouse.x <= ox + trackW &&
			                         mouse.y >= rowY && mouse.y < rowY + rowH;
			if (!anyKfRightClicked && inTrackArea &&
			    ImGui::IsMouseClicked(1) && ImGui::IsWindowHovered())
			{
				selRowIdx_    = rowIdx;
				selKeyTime_   = -1.f;
				ctxClickTime_ = std::clamp((mouse.x - ox) / zoomPxPerSec_, 0.f, duration);
				ImGui::OpenPopup("track_ctx");
			}
		}


		// ---- Keyframe Inspector ---------------------------------------------

		// Row のキーを「時刻 + X 値 + Y 値 + Ease」で編集する (単独行は X 値を Value として出す。設計書 §15.2)
		void UIAnimationEditor::DrawKeyframeInspector(UIAnimationClip& clip)
		{
			ImGui::Text("Keyframe");
			ImGui::Separator();

			const auto rows = BuildTimelineRows(clip);
			if (selRowIdx_ < 0 || selRowIdx_ >= (int)rows.size() || selKeyTime_ < 0.f)
			{
				ImGui::TextDisabled("Select a keyframe");
				return;
			}

			const auto& row    = rows[selRowIdx_];
			auto&       trackX = clip.tracks[row.trackIdx[0]];
			auto* const trackY = (row.trackIdx[1] >= 0) ? &clip.tracks[row.trackIdx[1]] : nullptr;

			const int xi = FindKeyIndexAtTime(trackX.keyframes, selKeyTime_);
			const int yi = trackY ? FindKeyIndexAtTime(trackY->keyframes, selKeyTime_) : -1;
			if (xi < 0 && yi < 0)
			{
				ImGui::TextDisabled("Select a keyframe");
				return;
			}

			// Time (両方に効く)
			float timeVal = selKeyTime_;
			ImGui::SetNextItemWidth(100.f);
			ImGui::DragFloat("Time", &timeVal, 0.001f, 0.f, clip.duration, "%.3f");
			if (ImGui::IsItemEdited())
			{
				timeVal = std::clamp(timeVal, 0.f, clip.duration);
				if (xi >= 0) trackX.keyframes[xi].time = timeVal;
				if (yi >= 0) trackY->keyframes[yi].time = timeVal;
				selKeyTime_ = timeVal;
				UIEditorSession::Get().dirty = true;
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				if (xi >= 0) SortKeyframes(trackX.keyframes);
				if (yi >= 0) SortKeyframes(trackY->keyframes);
			}

			// X / Y (組の行) または Value (単独行)。片方だけの時刻では無い側を Disabled にする
			if (trackY)
			{
				if (xi < 0) ImGui::BeginDisabled();
				float xVal = (xi >= 0) ? trackX.keyframes[xi].value : trackX.Sample(selKeyTime_);
				ImGui::SetNextItemWidth(100.f);
				ImGui::DragFloat("X", &xVal, 0.01f, -9999.f, 9999.f, "%.3f");
				if (xi >= 0 && ImGui::IsItemEdited())
				{
					trackX.keyframes[xi].value = xVal;
					UIEditorSession::Get().dirty = true;
				}
				if (xi < 0) ImGui::EndDisabled();

				if (yi < 0) ImGui::BeginDisabled();
				float yVal = (yi >= 0) ? trackY->keyframes[yi].value : trackY->Sample(selKeyTime_);
				ImGui::SetNextItemWidth(100.f);
				ImGui::DragFloat("Y", &yVal, 0.01f, -9999.f, 9999.f, "%.3f");
				if (yi >= 0 && ImGui::IsItemEdited())
				{
					trackY->keyframes[yi].value = yVal;
					UIEditorSession::Get().dirty = true;
				}
				if (yi < 0) ImGui::EndDisabled();
			}
			else
			{
				float val = trackX.keyframes[xi].value;
				ImGui::SetNextItemWidth(100.f);
				ImGui::DragFloat("Value", &val, 0.01f, -9999.f, 9999.f, "%.3f");
				if (ImGui::IsItemEdited())
				{
					trackX.keyframes[xi].value = val;
					UIEditorSession::Get().dirty = true;
				}
			}

			// Ease (両方に効く)
			static const char* EASE_LABELS[] =
				{ "Linear","EaseIn","EaseOut","EaseInOut","Bezier" };
			const EaseType curEase = (xi >= 0) ? trackX.keyframes[xi].ease : trackY->keyframes[yi].ease;
			int easeIdx = static_cast<int>(curEase);
			ImGui::SetNextItemWidth(120.f);
			if (ImGui::Combo("Ease", &easeIdx, EASE_LABELS, 5))
			{
				if (xi >= 0) trackX.keyframes[xi].ease = static_cast<EaseType>(easeIdx);
				if (yi >= 0) trackY->keyframes[yi].ease = static_cast<EaseType>(easeIdx);
				UIEditorSession::Get().dirty = true;
			}

			// Delete ボタン or Delete キー
			const bool deletePressed =
				ImGui::SmallButton("Delete Keyframe") ||
				(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				 ImGui::IsKeyPressed(ImGuiKey_Delete));

			if (deletePressed)
			{
				EraseSelectedKey(clip, row, selKeyTime_);
				selKeyTime_ = -1.f;
				UIEditorSession::Get().dirty = true;
				return;
			}

			// 時刻順ソート (編集後。時刻ベースの選択なのでソートしても選択は失われない)
			ImGui::SameLine();
			if (ImGui::SmallButton("Sort"))
			{
				if (xi >= 0) SortKeyframes(trackX.keyframes);
				if (yi >= 0) SortKeyframes(trackY->keyframes);
			}
		}


		// ---- Preview Controls -----------------------------------------------

		void UIAnimationEditor::DrawPreviewControls(
			UIObject* obj, UIAnimationComponent* anim, UIAnimationClip& clip, float dt)
		{
			ImGui::Text("Preview");
			ImGui::Separator();

			// ---- 実再生 (ランタイム経由。設計書 §10.4) ----
			if (ImGui::Button("Play"))
			{
				switch (clip.condition)
				{
				case UIClipCondition::Manual:  anim->Play(clip.group); break;
				case UIClipCondition::Trigger: anim->Trigger(clip.conditionParam); break;
				case UIClipCondition::Bool:    anim->SetCondition(clip.conditionParam, true); break;
				}
			}
			ImGui::SameLine();
			{
				const bool stopDisabled = (clip.condition == UIClipCondition::Trigger);
				if (stopDisabled) ImGui::BeginDisabled();
				if (ImGui::Button("Stop"))
				{
					if (clip.condition == UIClipCondition::Manual)
						anim->Stop(clip.group);
					else if (clip.condition == UIClipCondition::Bool)
						anim->SetCondition(clip.conditionParam, false);
				}
				if (stopDisabled) ImGui::EndDisabled();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(plays the real clip through the component)");

			ImGui::Separator();

			// ---- スクラブ再生 (エディタのスナップショットへ直書きする擬似再生。キー編集の補助) ----
			ImGui::TextDisabled("Scrub play (preview only, does not use Play/Stop):");

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

			// スクラブ時刻でキーフレーム追加 (選択 Row の両 Track に同時刻で。設計書 §15.2)
			ImGui::SameLine();
			const auto rows      = BuildTimelineRows(clip);
			const bool canAddKey = selRowIdx_ >= 0 && selRowIdx_ < (int)rows.size();
			if (!canAddKey) ImGui::BeginDisabled();
			if (ImGui::Button("+ Key"))
			{
				AddKeyToRow(clip, obj, rows[selRowIdx_], scrubTime_);
				selKeyTime_ = scrubTime_;
				UIEditorSession::Get().dirty = true;
			}
			if (!canAddKey) ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a keyframe to the selected row at the scrub time");

			// オートキー録画のトグル。ここは Clip 選択中しか描かれないので Disabled は要らない
			ImGui::SameLine();
			{
				bool recording = isRecording_;
				if (ImGui::Checkbox("Rec", &recording))
				{
					isRecording_ = recording;
					if (isRecording_)
					{
						// スクラブ再生中のまま録画を始めるとキーを打つ時刻が動いてしまうので止める
						isPlaying_ = false;

						// 0 秒キーの値をここで固定し、同時に Reset で録画開始前へ戻せるようにする
						TakeRecordingBaseline(obj);
						if (!hasSnapshot_) { TakeSnapshot(obj, clip); hasSnapshot_ = true; }
					}
					else
					{
						recBaseline_.clear();
					}
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Record: edits in the Properties tab drop keys at the scrub time");
			}
			if (isRecording_)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.f, 0.25f, 0.25f, 1.f), "\xe2\x97\x8f" " REC");
			}

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

			// 検証エラー: 同じ起動単位のプロパティ重複などがあれば赤字で 1 行
			{
				std::vector<UIAnimationSerializer::ValidationError> errors;
				if (!UIAnimationSerializer::ValidateDetailed(anim->GetClips(), errors))
				{
					ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f),
					                    "%zu animation error(s) - see Animation tab", errors.size());
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


		// ---- オートキー録画 --------------------------------------------------

		// Rec を ON にした瞬間の値を録画対象プロパティぶん控える。
		// 対応するコンポーネントが無いプロパティは ReadFrom() が 0 を返すが、
		// その場合は Track も作られないので使われない
		void UIAnimationEditor::TakeRecordingBaseline(UIObject* obj)
		{
			recBaseline_.clear();
			if (!obj) return;

			for (const UIAnimatedProperty prop : RECORDABLE_PROPERTIES)
			{
				UIAnimationTrack tmp;
				tmp.property = prop;
				recBaseline_[prop] = tmp.ReadFrom(obj);
			}
		}


		// Properties タブのウィジェットが編集された直後に UIEditorDebugPanel から呼ばれる。
		// 実再生 (Play) の時刻ではなくスクラブ時刻に対してだけ効く
		void UIAnimationEditor::RecordEdit(UIObject* obj, std::initializer_list<UIAnimatedProperty> props)
		{
			if (!isRecording_ || !obj) return;

			auto* anim = obj->GetComponent<UIAnimationComponent>();
			if (!anim) return;
			if (selClipIdx_ < 0 || selClipIdx_ >= (int)anim->GetClips().size()) return;

			auto& clip = anim->EditClip(static_cast<size_t>(selClipIdx_));

			bool touched = false;
			for (const UIAnimatedProperty prop : props)
			{
				int trackIdx = -1;
				for (int i = 0; i < (int)clip.tracks.size(); ++i)
				{
					if (clip.tracks[i].property == prop) { trackIdx = i; break; }
				}

				// PositionZ は深度ソート用なので自動では Track を作らない。既にあるときだけ打つ
				if (trackIdx < 0 && prop == UIAnimatedProperty::PositionZ) continue;

				if (trackIdx < 0)
				{
					UIAnimationTrack track;
					track.property = prop;

					// キー 1 本だけの Track は「0 秒から動く」ではなく「常にその値」になってしまうため、
					// 新規 Track に限り 0 秒へ録画開始時の値を置く (既存 Track の曲線は変えない)
					if (scrubTime_ > 0.f)
					{
						const auto  it   = recBaseline_.find(prop);
						const float base = (it != recBaseline_.end()) ? it->second : track.ReadFrom(obj);
						track.keyframes.push_back(UIKeyframe{ 0.f, base, EaseType::Linear });
					}

					trackIdx = (int)clip.tracks.size();
					clip.tracks.push_back(std::move(track));
				}

				SetKeyAtTime(clip.tracks[trackIdx], obj, scrubTime_);
				touched = true;
			}

			if (touched)
				UIEditorSession::Get().dirty = true;
		}


		// Properties タブ先頭の赤帯。選択 Clip 名とスクラブ時刻を出す
		std::string UIAnimationEditor::GetRecordingLabel(const UIObject* obj) const
		{
			if (!isRecording_) return std::string();

			const char* clipName = "(no clip)";
			if (obj)
			{
				if (const auto* anim = obj->GetComponent<UIAnimationComponent>())
				{
					const auto& clips = anim->GetClips();
					if (selClipIdx_ >= 0 && selClipIdx_ < (int)clips.size())
						clipName = clips[static_cast<size_t>(selClipIdx_)].name.c_str();
				}
			}

			char buf[192];
			std::snprintf(buf, sizeof(buf), "\xe2\x97\x8f" " REC  %s  @ %.2fs", clipName, scrubTime_);
			return std::string(buf);
		}

	} // namespace ui
} // namespace aq
#endif
