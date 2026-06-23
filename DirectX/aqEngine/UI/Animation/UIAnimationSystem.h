#pragma once

namespace aq
{
	namespace ui
	{
		class UIScreenManager;
		class UIObject;

		// UIAnimationSystem: UIScreenManager のスタックを走査し、
		// 各 UIObject の UIAnimationComponent を更新するシステム。
		// UIScreenManager::Update() の先頭から呼ばれる。
		class UIAnimationSystem
		{
		public:
			static void Update(UIScreenManager& screens, float dt);

		private:
			static void UpdateObject(UIObject* obj, float dt);
		};

	} // namespace ui
} // namespace aq
