#include "aq.h"
#include "UIAnimationTrack.h"
#include "UI/UIObject.h"
#include "UI/Component/UITransformComponent.h"
#include "UI/Component/UIImageComponent.h"
#include "UI/Component/UINineSliceComponent.h"
#include "UI/Component/UICircleGaugeComponent.h"
#include <cmath>

namespace aq
{
	namespace ui
	{
		namespace
		{
			float ApplyEasing(float t, EaseType ease)
			{
				switch (ease)
				{
					case EaseType::Linear:   return t;
					case EaseType::EaseIn:   return t * t;
					case EaseType::EaseOut:  return t * (2.f - t);
					case EaseType::EaseInOut:
						return (t < 0.5f) ? (2.f * t * t) : (-1.f + (4.f - 2.f * t) * t);
					case EaseType::Bezier:   return t * t * (3.f - 2.f * t); // smoothstep
					default:                 return t;
				}
			}
		}


		float UIAnimationTrack::Sample(float t) const
		{
			if (keyframes.empty()) return 0.f;
			if (keyframes.size() == 1 || t <= keyframes.front().time)
				return keyframes.front().value;
			if (t >= keyframes.back().time)
				return keyframes.back().value;

			// 時刻 t の直後キーフレームを二分探索
			auto it = std::lower_bound(keyframes.begin(), keyframes.end(), t,
				[](const UIKeyframe& kf, float time) { return kf.time < time; });

			const UIKeyframe& b = *it;
			const UIKeyframe& a = *(it - 1);

			const float span = b.time - a.time;
			if (span <= 0.f) return b.value;

			float alpha = (t - a.time) / span;          // [0, 1]
			alpha       = ApplyEasing(alpha, a.ease);   // イージング適用
			return a.value + (b.value - a.value) * alpha;
		}


		void UIAnimationTrack::Apply(UIObject* obj, float v) const
		{
			if (!obj) return;
			switch (property)
			{
				// ---- Transform ----
				case UIAnimatedProperty::PositionX:
					if (auto* t = obj->GetComponent<UITransformComponent>()) t->localPosition.x = v;
					break;
				case UIAnimatedProperty::PositionY:
					if (auto* t = obj->GetComponent<UITransformComponent>()) t->localPosition.y = v;
					break;
				case UIAnimatedProperty::PositionZ:
					if (auto* t = obj->GetComponent<UITransformComponent>()) t->localPosition.z = v;
					break;
				case UIAnimatedProperty::ScaleX:
					if (auto* t = obj->GetComponent<UITransformComponent>()) t->localScale.x = v;
					break;
				case UIAnimatedProperty::ScaleY:
					if (auto* t = obj->GetComponent<UITransformComponent>()) t->localScale.y = v;
					break;
				case UIAnimatedProperty::Rotation:
					if (auto* t = obj->GetComponent<UITransformComponent>()) t->rotation = v;
					break;
				case UIAnimatedProperty::SizeDeltaX:
					if (auto* t = obj->GetComponent<UITransformComponent>()) t->sizeDelta.x = v;
					break;
				case UIAnimatedProperty::SizeDeltaY:
					if (auto* t = obj->GetComponent<UITransformComponent>()) t->sizeDelta.y = v;
					break;
				case UIAnimatedProperty::Active:
					if (auto* t = obj->GetComponent<UITransformComponent>()) t->active = (v > 0.5f);
					break;

				// ---- Color (Image / NineSlice / CircleGauge 共通) ----
				case UIAnimatedProperty::ColorR:
					if (auto* c = obj->GetComponent<UIImageComponent>())      c->color.x = v;
					if (auto* c = obj->GetComponent<UINineSliceComponent>())  c->color.x = v;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->color.x = v;
					break;
				case UIAnimatedProperty::ColorG:
					if (auto* c = obj->GetComponent<UIImageComponent>())      c->color.y = v;
					if (auto* c = obj->GetComponent<UINineSliceComponent>())  c->color.y = v;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->color.y = v;
					break;
				case UIAnimatedProperty::ColorB:
					if (auto* c = obj->GetComponent<UIImageComponent>())      c->color.z = v;
					if (auto* c = obj->GetComponent<UINineSliceComponent>())  c->color.z = v;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->color.z = v;
					break;
				case UIAnimatedProperty::ColorA:
					if (auto* c = obj->GetComponent<UIImageComponent>())      c->color.w = v;
					if (auto* c = obj->GetComponent<UINineSliceComponent>())  c->color.w = v;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->color.w = v;
					break;

				// ---- FillAmount ----
				case UIAnimatedProperty::FillAmount:
					if (auto* c = obj->GetComponent<UIImageComponent>())       c->fillAmount = v;
					if (auto* c = obj->GetComponent<UINineSliceComponent>())   c->fillAmount = v;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->fillAmount = v;
					break;

				// ---- NineSlice border ----
				case UIAnimatedProperty::NineSliceBorderLeft:
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) c->border.left = v;
					break;
				case UIAnimatedProperty::NineSliceBorderRight:
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) c->border.right = v;
					break;
				case UIAnimatedProperty::NineSliceBorderTop:
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) c->border.top = v;
					break;
				case UIAnimatedProperty::NineSliceBorderBottom:
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) c->border.bottom = v;
					break;
			}
		}


		float UIAnimationTrack::ReadFrom(const UIObject* obj) const
		{
			if (!obj) return 0.f;
			switch (property)
			{
				case UIAnimatedProperty::PositionX:
					if (auto* t = obj->GetComponent<UITransformComponent>()) return t->localPosition.x;
					break;
				case UIAnimatedProperty::PositionY:
					if (auto* t = obj->GetComponent<UITransformComponent>()) return t->localPosition.y;
					break;
				case UIAnimatedProperty::PositionZ:
					if (auto* t = obj->GetComponent<UITransformComponent>()) return t->localPosition.z;
					break;
				case UIAnimatedProperty::ScaleX:
					if (auto* t = obj->GetComponent<UITransformComponent>()) return t->localScale.x;
					break;
				case UIAnimatedProperty::ScaleY:
					if (auto* t = obj->GetComponent<UITransformComponent>()) return t->localScale.y;
					break;
				case UIAnimatedProperty::Rotation:
					if (auto* t = obj->GetComponent<UITransformComponent>()) return t->rotation;
					break;
				case UIAnimatedProperty::SizeDeltaX:
					if (auto* t = obj->GetComponent<UITransformComponent>()) return t->sizeDelta.x;
					break;
				case UIAnimatedProperty::SizeDeltaY:
					if (auto* t = obj->GetComponent<UITransformComponent>()) return t->sizeDelta.y;
					break;
				case UIAnimatedProperty::Active:
					if (auto* t = obj->GetComponent<UITransformComponent>()) return t->active ? 1.f : 0.f;
					break;
				case UIAnimatedProperty::ColorR:
					if (auto* c = obj->GetComponent<UIImageComponent>()) return c->color.x;
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->color.x;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->color.x;
					break;
				case UIAnimatedProperty::ColorG:
					if (auto* c = obj->GetComponent<UIImageComponent>()) return c->color.y;
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->color.y;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->color.y;
					break;
				case UIAnimatedProperty::ColorB:
					if (auto* c = obj->GetComponent<UIImageComponent>()) return c->color.z;
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->color.z;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->color.z;
					break;
				case UIAnimatedProperty::ColorA:
					if (auto* c = obj->GetComponent<UIImageComponent>()) return c->color.w;
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->color.w;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->color.w;
					break;
				case UIAnimatedProperty::FillAmount:
					if (auto* c = obj->GetComponent<UIImageComponent>()) return c->fillAmount;
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->fillAmount;
					if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->fillAmount;
					break;
				case UIAnimatedProperty::NineSliceBorderLeft:
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->border.left;
					break;
				case UIAnimatedProperty::NineSliceBorderRight:
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->border.right;
					break;
				case UIAnimatedProperty::NineSliceBorderTop:
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->border.top;
					break;
				case UIAnimatedProperty::NineSliceBorderBottom:
					if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->border.bottom;
					break;
			}
			return 0.f;
		}

	} // namespace ui
} // namespace aq
