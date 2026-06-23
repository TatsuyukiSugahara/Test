#pragma once
#include <string>
#include <vector>
#include "UIClipTrack.h"

namespace aq
{
	namespace ui
	{
		// 1アニメーションクリップ: 複数の UIClipTrack と合計 duration を持つ。
		// UIAnimationComponent::clips に名前付きで登録して使う。
		struct UIAnimationClip
		{
			std::string              name;
			float                    duration   = 0.f;
			std::vector<UIClipTrack> clipTracks;
		};

	} // namespace ui
} // namespace aq
