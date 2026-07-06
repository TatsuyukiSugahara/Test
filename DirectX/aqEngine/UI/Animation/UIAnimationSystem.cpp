#include "aq.h"
#include "UIAnimationSystem.h"
#include "UI/UIObject.h"
#include "UI/Screen/UIScreen.h"
#include "UI/Component/UIAnimationComponent.h"

namespace aq
{
	namespace ui
	{
		void UIAnimationSystem::Update(UIScreenManager& screens, float dt)
		{
			for (int i = 0; i < screens.StackSize(); ++i)
			{
				UIScreen* screen = screens.GetScreen(i);
				if (!screen || !screen->GetRoot()) continue;
				UpdateObject(screen->GetRoot(), dt);
			}
		}

		void UIAnimationSystem::UpdateObject(UIObject* obj, float dt)
		{
			if (!obj || !obj->IsActiveInHierarchy()) return;

			if (auto* anim = obj->GetComponent<UIAnimationComponent>())
				anim->Update(dt);

			for (UIObject* child : obj->GetChildren())
				UpdateObject(child, dt);
		}

	} // namespace ui
} // namespace aq
