#include "aq.h"
#include "UIAnimationSerializer.h"
#include "UI/Component/UIAnimationComponent.h"

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

		const char* UIAnimationSerializer::ConditionToStr(UITrackCondition c)
		{
			switch (c)
			{
				case UITrackCondition::Bool:    return "Bool";
				case UITrackCondition::Trigger: return "Trigger";
				default:                        return "Default";
			}
		}

		UITrackCondition UIAnimationSerializer::StrToCondition(std::string_view s)
		{
			if (s == "Bool")    return UITrackCondition::Bool;
			if (s == "Trigger") return UITrackCondition::Trigger;
			return UITrackCondition::Default;
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

		// ---- UIClipTrack ----------------------------------------------------

		JV UIAnimationSerializer::SaveClipTrack(const UIClipTrack& track)
		{
			JV j = JV::MakeObject();
			j.Set("name",              JV(track.name));
			j.Set("condition",         JV(std::string(ConditionToStr(track.condition))));
			j.Set("conditionParam",    JV(track.conditionParam));
			j.Set("loopFrom",          JV(static_cast<double>(track.loopFrom)));
			j.Set("loopSkipFirst",     JV(track.loopSkipFirst));
			j.Set("restoreOnComplete", JV(track.restoreOnComplete));
			JV tracks = JV::MakeArray();
			for (const auto& t : track.tracks)
				tracks.PushBack(SavePropTrack(t));
			j.Set("tracks", std::move(tracks));
			return j;
		}

		UIClipTrack UIAnimationSerializer::LoadClipTrack(const JV& json)
		{
			UIClipTrack ct;
			ct.name              = json["name"].AsString();
			ct.condition         = StrToCondition(json["condition"].AsString());
			ct.conditionParam    = json["conditionParam"].AsString();
			ct.loopFrom          = json["loopFrom"].AsFloat(-1.f);
			ct.loopSkipFirst     = json["loopSkipFirst"].AsBool();
			ct.restoreOnComplete = json["restoreOnComplete"].AsBool();
			const auto& tracks = json["tracks"];
			if (tracks.IsArray())
			{
				for (size_t i = 0; i < tracks.Size(); ++i)
					ct.tracks.push_back(LoadPropTrack(tracks[i]));
			}
			return ct;
		}

		// ---- UIAnimationClip ------------------------------------------------

		JV UIAnimationSerializer::SaveClip(const UIAnimationClip& clip)
		{
			JV j = JV::MakeObject();
			j.Set("name",     JV(clip.name));
			j.Set("duration", JV(static_cast<double>(clip.duration)));
			JV cts = JV::MakeArray();
			for (const auto& ct : clip.clipTracks)
				cts.PushBack(SaveClipTrack(ct));
			j.Set("clipTracks", std::move(cts));
			return j;
		}

		UIAnimationClip UIAnimationSerializer::LoadClip(const JV& json)
		{
			UIAnimationClip clip;
			clip.name     = json["name"].AsString();
			clip.duration = json["duration"].AsFloat();
			const auto& cts = json["clipTracks"];
			if (cts.IsArray())
			{
				for (size_t i = 0; i < cts.Size(); ++i)
					clip.clipTracks.push_back(LoadClipTrack(cts[i]));
			}
			return clip;
		}

		// ---- UIAnimationComponent 全体 --------------------------------------

		JV UIAnimationSerializer::SaveAll(const UIAnimationComponent& comp)
		{
			JV j = JV::MakeObject();
			JV clips = JV::MakeArray();
			for (const auto& [name, clip] : comp.clips)
				clips.PushBack(SaveClip(clip));
			j.Set("clips", std::move(clips));
			return j;
		}

		void UIAnimationSerializer::LoadAll(const JV& json, UIAnimationComponent& comp)
		{
			const auto& clips = json["clips"];
			if (!clips.IsArray()) return;
			for (size_t i = 0; i < clips.Size(); ++i)
			{
				UIAnimationClip clip = LoadClip(clips[i]);
				if (!clip.name.empty())
					comp.clips[clip.name] = std::move(clip);
			}
		}

	} // namespace ui
} // namespace aq
