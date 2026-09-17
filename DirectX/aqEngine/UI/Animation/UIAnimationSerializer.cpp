#include "aq.h"
#include "UIAnimationSerializer.h"
#include "UI/Component/UIAnimationComponent.h"
#include <algorithm>
#include <string>
#include <unordered_map>

namespace aq
{
	namespace ui
	{
		using JV = util::JsonValue;

		// ---- enum <-> string ------------------------------------------------

		const char* UIAnimationSerializer::PropertyToStr(UIAnimatedProperty p)
		{
			switch (p)
			{
				case UIAnimatedProperty::PositionX:            return "PositionX";
				case UIAnimatedProperty::PositionY:            return "PositionY";
				case UIAnimatedProperty::PositionZ:            return "PositionZ";
				case UIAnimatedProperty::ScaleX:               return "ScaleX";
				case UIAnimatedProperty::ScaleY:               return "ScaleY";
				case UIAnimatedProperty::Rotation:             return "Rotation";
				case UIAnimatedProperty::SizeDeltaX:           return "SizeDeltaX";
				case UIAnimatedProperty::SizeDeltaY:           return "SizeDeltaY";
				case UIAnimatedProperty::ColorR:               return "ColorR";
				case UIAnimatedProperty::ColorG:               return "ColorG";
				case UIAnimatedProperty::ColorB:               return "ColorB";
				case UIAnimatedProperty::ColorA:               return "ColorA";
				case UIAnimatedProperty::FillAmount:           return "FillAmount";
				case UIAnimatedProperty::Active:               return "Active";
				case UIAnimatedProperty::NineSliceBorderLeft:  return "NineSliceBorderLeft";
				case UIAnimatedProperty::NineSliceBorderRight: return "NineSliceBorderRight";
				case UIAnimatedProperty::NineSliceBorderTop:   return "NineSliceBorderTop";
				case UIAnimatedProperty::NineSliceBorderBottom:return "NineSliceBorderBottom";
				case UIAnimatedProperty::TextCharCount:        return "TextCharCount";
				default:                                       return "PositionX";
			}
		}

		UIAnimatedProperty UIAnimationSerializer::StrToProperty(std::string_view s)
		{
			if (s == "PositionX")             return UIAnimatedProperty::PositionX;
			if (s == "PositionY")             return UIAnimatedProperty::PositionY;
			if (s == "PositionZ")             return UIAnimatedProperty::PositionZ;
			if (s == "ScaleX")                return UIAnimatedProperty::ScaleX;
			if (s == "ScaleY")                return UIAnimatedProperty::ScaleY;
			if (s == "Rotation")              return UIAnimatedProperty::Rotation;
			if (s == "SizeDeltaX")            return UIAnimatedProperty::SizeDeltaX;
			if (s == "SizeDeltaY")            return UIAnimatedProperty::SizeDeltaY;
			if (s == "ColorR")                return UIAnimatedProperty::ColorR;
			if (s == "ColorG")                return UIAnimatedProperty::ColorG;
			if (s == "ColorB")                return UIAnimatedProperty::ColorB;
			if (s == "ColorA")                return UIAnimatedProperty::ColorA;
			if (s == "FillAmount")            return UIAnimatedProperty::FillAmount;
			if (s == "Active")                return UIAnimatedProperty::Active;
			if (s == "NineSliceBorderLeft")   return UIAnimatedProperty::NineSliceBorderLeft;
			if (s == "NineSliceBorderRight")  return UIAnimatedProperty::NineSliceBorderRight;
			if (s == "NineSliceBorderTop")    return UIAnimatedProperty::NineSliceBorderTop;
			if (s == "NineSliceBorderBottom") return UIAnimatedProperty::NineSliceBorderBottom;
			if (s == "TextCharCount")         return UIAnimatedProperty::TextCharCount;
			return UIAnimatedProperty::PositionX;
		}

		const char* UIAnimationSerializer::EaseToStr(EaseType e)
		{
			switch (e)
			{
				case EaseType::EaseIn:    return "EaseIn";
				case EaseType::EaseOut:   return "EaseOut";
				case EaseType::EaseInOut: return "EaseInOut";
				case EaseType::Bezier:    return "Bezier";
				default:                  return "Linear";
			}
		}

		EaseType UIAnimationSerializer::StrToEase(std::string_view s)
		{
			if (s == "EaseIn")    return EaseType::EaseIn;
			if (s == "EaseOut")   return EaseType::EaseOut;
			if (s == "EaseInOut") return EaseType::EaseInOut;
			if (s == "Bezier")    return EaseType::Bezier;
			return EaseType::Linear;
		}

		const char* UIAnimationSerializer::ConditionToStr(UIClipCondition c)
		{
			switch (c)
			{
				case UIClipCondition::Bool:    return "Bool";
				case UIClipCondition::Trigger: return "Trigger";
				default:                       return "Manual";
			}
		}

		UIClipCondition UIAnimationSerializer::StrToCondition(std::string_view s)
		{
			if (s == "Bool")    return UIClipCondition::Bool;
			if (s == "Trigger") return UIClipCondition::Trigger;
			return UIClipCondition::Manual;
		}

		const char* UIAnimationSerializer::FinishModeToStr(UIAnimationFinishMode m)
		{
			switch (m)
			{
				case UIAnimationFinishMode::Restore: return "Restore";
				default:                              return "Hold";
			}
		}

		UIAnimationFinishMode UIAnimationSerializer::StrToFinishMode(std::string_view s)
		{
			if (s == "Restore") return UIAnimationFinishMode::Restore;
			return UIAnimationFinishMode::Hold;
		}

		// ---- Keyframe -------------------------------------------------------

		JV UIAnimationSerializer::SaveKeyframe(const UIKeyframe& kf)
		{
			JV j = JV::MakeObject();
			j.Set("time",  JV(static_cast<double>(kf.time)));
			j.Set("value", JV(static_cast<double>(kf.value)));
			j.Set("ease",  JV(std::string(EaseToStr(kf.ease))));
			return j;
		}

		UIKeyframe UIAnimationSerializer::LoadKeyframe(const JV& json)
		{
			UIKeyframe kf;
			kf.time  = json["time"].AsFloat();
			kf.value = json["value"].AsFloat();
			kf.ease  = StrToEase(json["ease"].AsString());
			return kf;
		}

		// ---- UIAnimationTrack (property track) ------------------------------

		JV UIAnimationSerializer::SavePropTrack(const UIAnimationTrack& track)
		{
			JV j = JV::MakeObject();
			j.Set("property", JV(std::string(PropertyToStr(track.property))));
			JV kfs = JV::MakeArray();
			for (const auto& kf : track.keyframes)
				kfs.PushBack(SaveKeyframe(kf));
			j.Set("keyframes", std::move(kfs));
			return j;
		}

		UIAnimationTrack UIAnimationSerializer::LoadPropTrack(const JV& json)
		{
			UIAnimationTrack t;
			t.property = StrToProperty(json["property"].AsString());
			const auto& kfs = json["keyframes"];
			if (kfs.IsArray())
			{
				for (size_t i = 0; i < kfs.Size(); ++i)
					t.keyframes.push_back(LoadKeyframe(kfs[i]));
			}
			return t;
		}

		// ---- UIAnimationClip (設計書 §4.4) -----------------------------------

		JV UIAnimationSerializer::SaveClip(const UIAnimationClip& clip)
		{
			JV j = JV::MakeObject();
			j.Set("name", JV(clip.name));
			if (!clip.groupName.empty())
				j.Set("group", JV(clip.groupName));
			j.Set("duration",  JV(static_cast<double>(clip.duration)));
			j.Set("condition", JV(std::string(ConditionToStr(clip.condition))));
			if (clip.condition != UIClipCondition::Manual && !clip.conditionParamName.empty())
				j.Set("conditionParam", JV(clip.conditionParamName));
			if (clip.loopFrom >= 0.f)
				j.Set("loopFrom", JV(static_cast<double>(clip.loopFrom)));
			j.Set("finish", JV(std::string(FinishModeToStr(clip.finish))));

			JV tracks = JV::MakeArray();
			for (const auto& t : clip.tracks)
				tracks.PushBack(SavePropTrack(t));
			j.Set("tracks", std::move(tracks));
			return j;
		}

		UIAnimationClip UIAnimationSerializer::LoadClip(const JV& json)
		{
			UIAnimationClip clip;
			clip.name               = json["name"].AsString();
			clip.groupName          = json["group"].AsString();
			clip.duration           = json["duration"].AsFloat();
			clip.condition          = StrToCondition(json["condition"].AsString());
			clip.conditionParamName = json["conditionParam"].AsString();
			clip.loopFrom           = json["loopFrom"].AsFloat(-1.f);
			clip.finish             = StrToFinishMode(json["finish"].AsString());

			// group / conditionParam はロード時にハッシュ化する (設計書 §4.3)
			clip.group          = clip.groupName.empty() ? aqHash32(clip.name.c_str()) : aqHash32(clip.groupName.c_str());
			clip.conditionParam = clip.conditionParamName.empty() ? 0u : aqHash32(clip.conditionParamName.c_str());

			const auto& tracks = json["tracks"];
			if (tracks.IsArray())
			{
				for (size_t i = 0; i < tracks.Size(); ++i)
					clip.tracks.push_back(LoadPropTrack(tracks[i]));
			}
			return clip;
		}

		// ---- authoring 検証 (設計書 §4.5) -------------------------------------

		bool UIAnimationSerializer::ValidateDetailed(const std::vector<UIAnimationClip>& clips, std::vector<ValidationError>& errors)
		{
			errors.clear();
			const size_t clipCount = clips.size();

			for (size_t i = 0; i < clipCount; ++i)
			{
				const UIAnimationClip& clip = clips[i];

				// クリップ名は非空、同一コンポーネント内で重複なし (先勝ちで後発を違反にする)
				if (clip.name.empty())
				{
					errors.push_back({ i, "clip name is empty" });
				}
				else
				{
					for (size_t j = 0; j < i; ++j)
					{
						if (clips[j].name == clip.name)
						{
							errors.push_back({ i, "duplicate clip name: '" + clip.name + "'" });
							break;
						}
					}
				}

				// Bool / Trigger の conditionParam は非空
				if (clip.condition != UIClipCondition::Manual && clip.conditionParamName.empty())
					errors.push_back({ i, "clip '" + clip.name + "': conditionParam is empty (Bool / Trigger)" });

				// duration > 0
				if (!(clip.duration > 0.f))
					errors.push_back({ i, "clip '" + clip.name + "': duration must be > 0" });

				// 0 <= loopFrom < duration (loopFrom == -1 はループなしなので対象外)
				if (clip.loopFrom >= 0.f && clip.loopFrom >= clip.duration)
					errors.push_back({ i, "clip '" + clip.name + "': loopFrom is outside [0, duration)" });

				// 同一 Clip 内で同じ property の Track は 1 本
				for (size_t a = 0; a < clip.tracks.size(); ++a)
				{
					for (size_t b = a + 1; b < clip.tracks.size(); ++b)
					{
						if (clip.tracks[a].property == clip.tracks[b].property)
						{
							errors.push_back({ i, "clip '" + clip.name + "': property '"
								+ PropertyToStr(clip.tracks[a].property) + "' has more than one track" });
							break;
						}
					}
				}

				// Exit グループの Manual クリップに loopFrom >= 0 を許さない (画面遷移が止まるため)
				if (clip.condition == UIClipCondition::Manual && clip.group == kUIAnimGroupExit && clip.loopFrom >= 0.f)
					errors.push_back({ i, "clip '" + clip.name + "': Exit group clips must not loop" });
			}

			// aqHash32() の結果が 0、または同一コンポーネント内で別文字列が同じハッシュ。
			// group と conditionParam は別の照合先 (Play / SetCondition・Trigger) なので、別々に集計する
			{
				std::unordered_map<uint32_t, std::string> groupLabelByHash;
				std::unordered_map<uint32_t, std::string> paramLabelByHash;

				for (size_t i = 0; i < clipCount; ++i)
				{
					const UIAnimationClip& clip = clips[i];
					const std::string groupLabel = clip.groupName.empty() ? clip.name : clip.groupName;

					if (clip.group == 0u)
					{
						errors.push_back({ i, "clip '" + clip.name + "': group hash is 0 (unresolved)" });
					}
					else
					{
						auto it = groupLabelByHash.find(clip.group);
						if (it == groupLabelByHash.end())
							groupLabelByHash.emplace(clip.group, groupLabel);
						else if (it->second != groupLabel)
							errors.push_back({ i, "clip '" + clip.name + "': group '" + groupLabel
								+ "' collides with '" + it->second + "' (CRC32 collision)" });
					}

					if (clip.condition != UIClipCondition::Manual && !clip.conditionParamName.empty())
					{
						if (clip.conditionParam == 0u)
						{
							errors.push_back({ i, "clip '" + clip.name + "': conditionParam hash is 0 (unresolved)" });
						}
						else
						{
							auto it = paramLabelByHash.find(clip.conditionParam);
							if (it == paramLabelByHash.end())
								paramLabelByHash.emplace(clip.conditionParam, clip.conditionParamName);
							else if (it->second != clip.conditionParamName)
								errors.push_back({ i, "clip '" + clip.name + "': conditionParam '" + clip.conditionParamName
									+ "' collides with '" + it->second + "' (CRC32 collision)" });
						}
					}
				}
			}

			// 同じ起動単位 (同 group の Manual 同士 / 同 param の Bool 同士 / 同 param の Trigger 同士) で
			// 同じ property を使うクリップは 1 本 (§6.2 の serial 競合を作らないため)
			{
				struct UsedProperty
				{
					UIClipCondition    condition;
					uint32_t           id; // Manual = group, Bool/Trigger = conditionParam
					UIAnimatedProperty property;
					size_t             ownerIndex;
				};
				std::vector<UsedProperty> used;

				for (size_t i = 0; i < clipCount; ++i)
				{
					const UIAnimationClip& clip = clips[i];
					const uint32_t id = (clip.condition == UIClipCondition::Manual) ? clip.group : clip.conditionParam;

					for (const auto& track : clip.tracks)
					{
						bool conflict = false;
						for (const auto& u : used)
						{
							if (u.condition == clip.condition && u.id == id && u.property == track.property)
							{
								errors.push_back({ i, "clip '" + clip.name + "': same activation unit as clip #" + std::to_string(u.ownerIndex)
									+ " uses property '" + PropertyToStr(track.property) + "' too" });
								conflict = true;
								break;
							}
						}
						if (!conflict)
							used.push_back({ clip.condition, id, track.property, i });
					}
				}
			}

			std::stable_sort(errors.begin(), errors.end(),
				[](const ValidationError& a, const ValidationError& b) { return a.clipIndex < b.clipIndex; });

			return errors.empty();
		}

		bool UIAnimationSerializer::Validate(const std::vector<UIAnimationClip>& clips, std::vector<std::string>& errors)
		{
			std::vector<ValidationError> detailed;
			const bool ok = ValidateDetailed(clips, detailed);

			errors.clear();
			errors.reserve(detailed.size());
			for (const auto& e : detailed)
				errors.push_back(e.message);
			return ok;
		}

		// ---- UIAnimationComponent 全体 ----------------------------------------

		JV UIAnimationSerializer::SaveAll(const UIAnimationComponent& comp)
		{
			JV j = JV::MakeObject();
			JV clips = JV::MakeArray();
			for (const auto& clip : comp.GetClips())
				clips.PushBack(SaveClip(clip));
			j.Set("clips", std::move(clips));
			return j;
		}

		void UIAnimationSerializer::LoadAll(const JV& json, UIAnimationComponent& comp)
		{
			std::vector<UIAnimationClip> clips;
			const auto& clipsJson = json["clips"];
			if (clipsJson.IsArray())
			{
				clips.reserve(clipsJson.Size());
				for (size_t i = 0; i < clipsJson.Size(); ++i)
					clips.push_back(LoadClip(clipsJson[i]));
			}

			// キーフレーム時刻を duration へ clamp する (設計書 §4.5。エラー扱いにはしない)
			for (auto& clip : clips)
			{
				for (auto& track : clip.tracks)
				{
					for (auto& kf : track.keyframes)
					{
						if (kf.time > clip.duration)
						{
							EnginePrintf("[UIAnim] clip '%s': keyframe time %.3f clamped to duration %.3f\n",
								clip.name.c_str(), kf.time, clip.duration);
							kf.time = clip.duration;
						}
					}
				}
			}

			// 検証違反のクリップは警告して捨てる
			std::vector<ValidationError> errors;
			ValidateDetailed(clips, errors);

			std::vector<bool> invalid(clips.size(), false);
			for (const auto& e : errors)
			{
				if (e.clipIndex >= invalid.size()) continue;
				if (!invalid[e.clipIndex])
					EnginePrintf("[UIAnim] clip discarded: %s\n", e.message.c_str());
				invalid[e.clipIndex] = true;
			}

			std::vector<UIAnimationClip> valid;
			valid.reserve(clips.size());
			for (size_t i = 0; i < clips.size(); ++i)
			{
				if (!invalid[i])
					valid.push_back(std::move(clips[i]));
			}

			comp.ReplaceAllClips(std::move(valid));
		}

	} // namespace ui
} // namespace aq
