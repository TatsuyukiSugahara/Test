#pragma once
#include <vector>
#include "UIAnimatedProperty.h"
#include "UI/UITypes.h"

namespace aq
{
	namespace ui
	{
		class UIObject;

		// 1キーフレーム (時刻・値・イージング)
		struct UIKeyframe
		{
			float    time  = 0.f;
			float    value = 0.f;
			EaseType ease  = EaseType::Linear;
		};

		// イージング関数。t は [0, 1]。どの種別も E(0)=0, E(1)=1 を満たす。
		// Back / Elastic は [0, 1] の外へ出る (クランプしない)。エディタの曲線プレビューも同じ関数を呼ぶ
		float EvaluateEase(float t, EaseType ease);


		// 1プロパティの時系列データ。
		// Sample() で現在時刻の補間値を返し、Apply() で UIObject に書き込む。
		struct UIAnimationTrack
		{
			UIAnimatedProperty      property = UIAnimatedProperty::PositionX;
			std::vector<UIKeyframe> keyframes;

			// 時刻 t (秒) での補間値を返す
			float Sample(float t) const;

			// obj の対応プロパティに値を書き込む
			void Apply(UIObject* obj, float value) const;

			// obj の現在値を読む (restoreOnComplete 用スナップショット取得)
			float ReadFrom(const UIObject* obj) const;
		};

	} // namespace ui
} // namespace aq
