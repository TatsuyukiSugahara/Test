#pragma once
#include "ActionMap.h"

namespace aq
{
	namespace hid
	{
		/**
		 * 物理入力とゲームアクションを仲介する薄い抽象レイヤー。
		 * TAction はゲーム側で定義した enum class (uint32_t ベース)。
		 * キーボード・マウス・ゲームパッドを統一して扱う。
		 * ActionMap のポインタを保持するだけで所有はしない。
		 */
		template<typename TAction>
		class ActionInput
		{
		public:
			void             SetActionMap(const ActionMap* map) { map_ = map; }
			const ActionMap* GetActionMap() const               { return map_; }

			bool IsTriggered(TAction action) const                         { return Query(action, QueryType::Triggered);         }
			bool IsPressed  (TAction action) const                         { return Query(action, QueryType::Pressed);           }
			bool IsReleased (TAction action) const                         { return Query(action, QueryType::Released);          }
			bool IsLongPress(TAction action, float threshold = 0.5f) const { return QueryLongPress(action, threshold);           }

			math::Vector2 GetStick(TAction action) const
			{
				if (!map_) return {};
				const auto* sb = map_->FindStick(static_cast<uint32_t>(action));
				if (!sb) return {};
				return math::Vector2(
					GetPadAxis(sb->padIndex, sb->axisX),
					GetPadAxis(sb->padIndex, sb->axisY));
			}

		private:
			enum class QueryType { Triggered, Pressed, Released };

			bool Query(TAction action, QueryType q) const
			{
				if (!map_) return false;
				const auto* bindings = map_->Find(static_cast<uint32_t>(action));
				if (!bindings) return false;
				for (const auto& b : *bindings)
					if (Eval(b, q)) return true;
				return false;
			}

			bool QueryLongPress(TAction action, float threshold) const
			{
				if (!map_) return false;
				const auto* bindings = map_->Find(static_cast<uint32_t>(action));
				if (!bindings) return false;
				for (const auto& b : *bindings)
					if (EvalLongPress(b, threshold)) return true;
				return false;
			}

			static bool Eval(const InputBinding& b, QueryType q)
			{
				switch (b.type)
				{
				case InputBinding::Type::Key:
					switch (q)
					{
					case QueryType::Triggered: return IsKeyTriggered(static_cast<KeyBoardType>(b.code));
					case QueryType::Pressed:   return IsKeyPressed  (static_cast<KeyBoardType>(b.code));
					case QueryType::Released:  return IsKeyReleased (static_cast<KeyBoardType>(b.code));
					default: break;
					}
					break;
				case InputBinding::Type::MouseButton:
					switch (q)
					{
					case QueryType::Triggered: return IsMouseTriggered(static_cast<MouseButton>(b.code));
					case QueryType::Pressed:   return IsMousePressed  (static_cast<MouseButton>(b.code));
					case QueryType::Released:  return IsMouseReleased (static_cast<MouseButton>(b.code));
					default: break;
					}
					break;
				case InputBinding::Type::PadButton:
					switch (q)
					{
					case QueryType::Triggered: return IsPadTriggered(b.padIndex, static_cast<PadButton>(b.code));
					case QueryType::Pressed:   return IsPadPressed  (b.padIndex, static_cast<PadButton>(b.code));
					case QueryType::Released:  return IsPadReleased (b.padIndex, static_cast<PadButton>(b.code));
					default: break;
					}
					break;
				}
				return false;
			}

			static bool EvalLongPress(const InputBinding& b, float threshold)
			{
				switch (b.type)
				{
				case InputBinding::Type::Key:         return IsKeyLongPress  (static_cast<KeyBoardType>(b.code),          threshold);
				case InputBinding::Type::MouseButton: return IsMouseLongPress(static_cast<MouseButton> (b.code),          threshold);
				case InputBinding::Type::PadButton:   return IsPadLongPress  (b.padIndex, static_cast<PadButton>(b.code), threshold);
				default: break;
				}
				return false;
			}

		private:
			const ActionMap* map_ = nullptr;
		};
	}
}
