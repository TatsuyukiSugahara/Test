#include "aq.h"
#include "UIAnimationTrack.h"
#include "UI/UIObject.h"
#include "UI/Component/UITransformComponent.h"
#include "UI/Component/UIImageComponent.h"
#include "UI/Component/UINineSliceComponent.h"
#include "UI/Component/UICircleGaugeComponent.h"
#include "UI/Component/UITextComponent.h"
#include <cmath>

namespace aq
{
	namespace ui
	{
		namespace
		{
			static constexpr float BACK_C1    = 1.70158f;
			static constexpr float BACK_C2    = BACK_C1 * 1.525f;
			static constexpr float BACK_C3    = BACK_C1 + 1.f;
			static constexpr float ELASTIC_C4 = 2.f * 3.14159265358979f / 3.f;
			static constexpr float ELASTIC_C5 = 2.f * 3.14159265358979f / 4.5f;
			static constexpr float BOUNCE_N1  = 7.5625f;
			static constexpr float BOUNCE_D1  = 2.75f;

			float BounceOut(float t)
			{
				if (t < 1.f / BOUNCE_D1) {
					return BOUNCE_N1 * t * t;
				} else if (t < 2.f / BOUNCE_D1) {
					t -= 1.5f / BOUNCE_D1;
					return BOUNCE_N1 * t * t + 0.75f;
				} else if (t < 2.5f / BOUNCE_D1) {
					t -= 2.25f / BOUNCE_D1;
					return BOUNCE_N1 * t * t + 0.9375f;
				}
				t -= 2.625f / BOUNCE_D1;
				return BOUNCE_N1 * t * t + 0.984375f;
			}
		}


		float EvaluateEase(float t, EaseType ease)
		{
			switch (ease)
			{
				case EaseType::Linear:     return t;
				case EaseType::EaseIn:     return t * t;
				case EaseType::EaseOut:    return t * (2.f - t);
				case EaseType::EaseInOut:
					return (t < 0.5f) ? (2.f * t * t) : (-1.f + (4.f - 2.f * t) * t);
				case EaseType::Smoothstep: return t * t * (3.f - 2.f * t);

				case EaseType::BackIn:
					return BACK_C3 * t * t * t - BACK_C1 * t * t;
				case EaseType::BackOut:
				{
					const float u = t - 1.f;
					return 1.f + BACK_C3 * u * u * u + BACK_C1 * u * u;
				}
				case EaseType::BackInOut:
					if (t < 0.5f) {
						const float u = 2.f * t;
						return (u * u * ((BACK_C2 + 1.f) * u - BACK_C2)) / 2.f;
					} else {
						const float u = 2.f * t - 2.f;
						return (u * u * ((BACK_C2 + 1.f) * u + BACK_C2) + 2.f) / 2.f;
					}

				// Elastic は式が端点で厳密に 0 / 1 にならないので先に分岐する
				case EaseType::ElasticIn:
					if (t <= 0.f) return 0.f;
					if (t >= 1.f) return 1.f;
					return -std::pow(2.f, 10.f * t - 10.f) * std::sin((10.f * t - 10.75f) * ELASTIC_C4);
				case EaseType::ElasticOut:
					if (t <= 0.f) return 0.f;
					if (t >= 1.f) return 1.f;
					return std::pow(2.f, -10.f * t) * std::sin((10.f * t - 0.75f) * ELASTIC_C4) + 1.f;
				case EaseType::ElasticInOut:
					if (t <= 0.f) return 0.f;
					if (t >= 1.f) return 1.f;
					if (t < 0.5f)
						return -(std::pow(2.f, 20.f * t - 10.f) * std::sin((20.f * t - 11.125f) * ELASTIC_C5)) / 2.f;
					return (std::pow(2.f, -20.f * t + 10.f) * std::sin((20.f * t - 11.125f) * ELASTIC_C5)) / 2.f + 1.f;

				case EaseType::BounceIn:   return 1.f - BounceOut(1.f - t);
				case EaseType::BounceOut:  return BounceOut(t);
				case EaseType::BounceInOut:
					return (t < 0.5f) ? (1.f - BounceOut(1.f - 2.f * t)) / 2.f
					                  : (1.f + BounceOut(2.f * t - 1.f)) / 2.f;
				default:                   return t;
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
			alpha       = EvaluateEase(alpha, a.ease);  // イージング適用
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

				// ---- Color (Image / NineSlice / CircleGauge は排他。持っている 1 つに書く) ----
				case UIAnimatedProperty::ColorR:
					if (auto* c = obj->GetComponent<UIImageComponent>())            c->color.x = v;
					else if (auto* c = obj->GetComponent<UINineSliceComponent>())   c->color.x = v;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->color.x = v;
					break;
				case UIAnimatedProperty::ColorG:
					if (auto* c = obj->GetComponent<UIImageComponent>())            c->color.y = v;
					else if (auto* c = obj->GetComponent<UINineSliceComponent>())   c->color.y = v;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->color.y = v;
					break;
				case UIAnimatedProperty::ColorB:
					if (auto* c = obj->GetComponent<UIImageComponent>())            c->color.z = v;
					else if (auto* c = obj->GetComponent<UINineSliceComponent>())   c->color.z = v;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->color.z = v;
					break;
				case UIAnimatedProperty::ColorA:
					if (auto* c = obj->GetComponent<UIImageComponent>())            c->color.w = v;
					else if (auto* c = obj->GetComponent<UINineSliceComponent>())   c->color.w = v;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->color.w = v;
					break;

				// ---- FillAmount (排他。持っている 1 つに書く) ----
				case UIAnimatedProperty::FillAmount:
					if (auto* c = obj->GetComponent<UIImageComponent>())            c->fillAmount = v;
					else if (auto* c = obj->GetComponent<UINineSliceComponent>())   c->fillAmount = v;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) c->fillAmount = v;
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

				// ---- Text ----
				case UIAnimatedProperty::TextCharCount:
					if (auto* c = obj->GetComponent<UITextComponent>())
						c->visibleCharCount = (v < 0.f) ? -1 : static_cast<int>(v);
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
					else if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->color.x;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->color.x;
					break;
				case UIAnimatedProperty::ColorG:
					if (auto* c = obj->GetComponent<UIImageComponent>()) return c->color.y;
					else if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->color.y;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->color.y;
					break;
				case UIAnimatedProperty::ColorB:
					if (auto* c = obj->GetComponent<UIImageComponent>()) return c->color.z;
					else if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->color.z;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->color.z;
					break;
				case UIAnimatedProperty::ColorA:
					if (auto* c = obj->GetComponent<UIImageComponent>()) return c->color.w;
					else if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->color.w;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->color.w;
					break;
				case UIAnimatedProperty::FillAmount:
					if (auto* c = obj->GetComponent<UIImageComponent>()) return c->fillAmount;
					else if (auto* c = obj->GetComponent<UINineSliceComponent>()) return c->fillAmount;
					else if (auto* c = obj->GetComponent<UICircleGaugeComponent>()) return c->fillAmount;
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

				// ---- Text ----
				case UIAnimatedProperty::TextCharCount:
					if (auto* c = obj->GetComponent<UITextComponent>())
						return static_cast<float>(c->visibleCharCount);
					break;
			}
			return 0.f;
		}

	} // namespace ui
} // namespace aq
