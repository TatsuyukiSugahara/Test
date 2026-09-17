#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "UIAnimationTrack.h"
#include "Util/CRC32.h"

namespace aq
{
	namespace ui
	{
		// UIAnimationClip の起動条件
		enum class UIClipCondition : uint8_t
		{
			Manual,  // Play(group) で起動
			Bool,    // SetCondition(conditionParam, true) の間アクティブ
			Trigger, // Trigger(conditionParam) で一回だけ起動
		};

		// クリップ完了後の挙動
		enum class UIAnimationFinishMode : uint8_t
		{
			Hold,    // 完了時の最終値を基準値へ確定する
			Restore, // 基準値を変えずにレイヤーを外す (起動前の値へ戻る)
		};

		// 1アニメーションクリップ: 条件・ループ・完了時動作と、動かすプロパティトラックのリストを持つ。
		// UIAnimationComponent::clips_ に登録して使う。
		struct UIAnimationClip
		{
			std::string            name;                                     // 表示・保存用。非空・重複禁止
			std::string            groupName;                                // 表示・保存用。空 = name と同じ
			uint32_t                group          = 0u;                     // 実行時の識別子。0 = 未解決
			float                   duration       = 0.f;                    // クリップごとに持つ
			UIClipCondition         condition      = UIClipCondition::Manual;
			uint32_t                conditionParam = 0u;                     // Bool / Trigger の識別子 (ハッシュ)
			std::string             conditionParamName;                      // 表示・保存用
			float                   loopFrom       = -1.f;                   // -1 = ループなし
			UIAnimationFinishMode   finish         = UIAnimationFinishMode::Hold;

			std::vector<UIAnimationTrack> tracks;
		};

		// Enter / Exit の既定グループ名ハッシュ (§8.1)。自動フックとローダの検証が参照する。
		inline constexpr uint32_t kUIAnimGroupEnter = aqHash32("Enter");
		inline constexpr uint32_t kUIAnimGroupExit  = aqHash32("Exit");

	} // namespace ui
} // namespace aq
