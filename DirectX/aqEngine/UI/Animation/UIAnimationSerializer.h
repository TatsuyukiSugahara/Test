#pragma once
#include "UIAnimationClip.h"
#include "Util/SimpleJson.h"

namespace aq
{
	namespace ui
	{
		class UIAnimationComponent;

		// UIAnimationClip <-> JSON 双方向変換。
		// UIDocumentLoader/Serializer の "animation" セクションと
		// UIAnimationEditor の保存処理から利用する。
		class UIAnimationSerializer
		{
		public:
			static util::JsonValue  SaveClip(const UIAnimationClip& clip);
			static UIAnimationClip  LoadClip(const util::JsonValue& json);

			// UIAnimationComponent が持つ全クリップを { "clips": [...] } 形式で保存/復元
			static util::JsonValue  SaveAll(const UIAnimationComponent& comp);
			static void             LoadAll(const util::JsonValue& json, UIAnimationComponent& comp);

			// enum <-> 文字列 (Editor から直接使えるよう public)
			static const char*         PropertyToStr(UIAnimatedProperty p);
			static UIAnimatedProperty  StrToProperty(std::string_view s);
			static const char*         EaseToStr(EaseType e);
			static EaseType            StrToEase(std::string_view s);
			static const char*         ConditionToStr(UITrackCondition c);
			static UITrackCondition    StrToCondition(std::string_view s);

		private:
			static util::JsonValue  SaveClipTrack(const UIClipTrack& track);
			static UIClipTrack      LoadClipTrack(const util::JsonValue& json);
			static util::JsonValue  SavePropTrack(const UIAnimationTrack& track);
			static UIAnimationTrack LoadPropTrack(const util::JsonValue& json);
			static util::JsonValue  SaveKeyframe(const UIKeyframe& kf);
			static UIKeyframe       LoadKeyframe(const util::JsonValue& json);
		};

	} // namespace ui
} // namespace aq
