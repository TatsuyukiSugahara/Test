#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "UIAnimationTrack.h"

namespace aq
{
	namespace ui
	{
		// UIClipTrack の起動条件
		enum class UITrackCondition : uint8_t
		{
			Default, // 常時アクティブ。クリップ完了判定を担う。
			Bool,    // SetCondition(conditionParam, true) の間アクティブ
			Trigger, // TriggerTrack(conditionParam) で一回だけ起動
		};

		// 1クリップトラック: 条件・ループ設定と、動かすプロパティトラックのリスト。
		struct UIClipTrack
		{
			std::string         name;
			UITrackCondition    condition         = UITrackCondition::Default;
			std::string         conditionParam;   // Bool / Trigger 時の識別名
			float               loopFrom          = -1.f; // -1=ループなし, >=0=ループ開始時刻
			bool                loopSkipFirst     = false; // 2回目以降はイントロをスキップ
			bool                restoreOnComplete = false; // 完了後にスナップショットへ戻す

			std::vector<UIAnimationTrack> tracks; // このトラックが駆動するプロパティトラック群
		};

	} // namespace ui
} // namespace aq
