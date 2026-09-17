#pragma once
#include "UIAnimationClip.h"
#include "Util/SimpleJson.h"
#include <string>
#include <string_view>
#include <vector>

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
			// 1件の検証違反。どのクリップ (clips 内の index) が違反したかを特定できる形にしておく
			struct ValidationError
			{
				size_t      clipIndex; // 違反したクリップの index
				std::string message;   // 日本語メッセージ (クリップ名と規則が分かる文言)
			};


		public:
			static util::JsonValue  SaveClip(const UIAnimationClip& clip);
			static UIAnimationClip  LoadClip(const util::JsonValue& json);

			// UIAnimationComponent が持つ全クリップを { "clips": [...] } 形式で保存/復元。
			// LoadAll はキーフレーム時刻を duration へ clamp したうえで Validate にかけ、
			// 違反したクリップを警告付きで捨て、残りを ReplaceAllClips() で全置換する
			static util::JsonValue  SaveAll(const UIAnimationComponent& comp);
			static void             LoadAll(const util::JsonValue& json, UIAnimationComponent& comp);

			// authoring 検証 (設計書 §4.5)。エラーが 0 件なら true。
			// Validate は errors にクリップ名込みのメッセージだけを積む簡易版、
			// ValidateDetailed はどのクリップ (index) が違反したかも積む詳細版
			static bool Validate(const std::vector<UIAnimationClip>& clips, std::vector<std::string>& errors);
			static bool ValidateDetailed(const std::vector<UIAnimationClip>& clips, std::vector<ValidationError>& errors);

			// enum <-> 文字列 (Editor から直接使えるよう public)
			static const char*           PropertyToStr(UIAnimatedProperty p);
			static UIAnimatedProperty    StrToProperty(std::string_view s);
			static const char*           EaseToStr(EaseType e);
			static EaseType              StrToEase(std::string_view s);
			static const char*           ConditionToStr(UIClipCondition c);
			static UIClipCondition       StrToCondition(std::string_view s);
			static const char*           FinishModeToStr(UIAnimationFinishMode m);
			static UIAnimationFinishMode StrToFinishMode(std::string_view s);

		private:
			static util::JsonValue  SavePropTrack(const UIAnimationTrack& track);
			static UIAnimationTrack LoadPropTrack(const util::JsonValue& json);
			static util::JsonValue  SaveKeyframe(const UIKeyframe& kf);
			static UIKeyframe       LoadKeyframe(const util::JsonValue& json);
		};

	} // namespace ui
} // namespace aq
