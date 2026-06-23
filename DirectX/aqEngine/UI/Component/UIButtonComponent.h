#pragma once
#include <functional>
#include "IUIComponent.h"

namespace aq
{
	namespace ui
	{
		class UIScreen;
		class UIScreenManager;

		// ボタン callback に渡すイベント情報
		struct UIClickEvent
		{
			UIObject&        sender;
			UIScreen&        screen;
			UIScreenManager& screens;
		};

		class UIButtonComponent : public IUIComponent
		{
		public:
			// ---- callback (OnCreate() でバインド) ----
			std::function<void(const UIClickEvent&)> onClick;
			std::function<void(const UIClickEvent&)> onHoverEnter;
			std::function<void(const UIClickEvent&)> onHoverExit;
			std::function<void(const UIClickEvent&)> onFocusEnter;
			std::function<void(const UIClickEvent&)> onFocusExit;

			bool interactable = true;

			// ---- UIInputSystem が毎フレーム更新する状態 ----
			bool isHovered = false;
			bool isFocused = false;
			bool isPressed = false;
		};

	} // namespace ui
} // namespace aq
