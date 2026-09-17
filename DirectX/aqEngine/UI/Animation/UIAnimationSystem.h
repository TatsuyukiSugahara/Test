#pragma once
#include <cstdint>

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

			// root 以下の全 UIAnimationComponent に Play(group) を掛ける (画面 Enter の自動フック用。設計書 §8.1)
			static void PlayGroup(UIObject* root, uint32_t group);

			// root 以下のどれかの UIAnimationComponent で group の Manual クリップが active なら true (設計書 §5.2)
			static bool IsAnimationGroupPlaying(const UIObject* root, uint32_t group);

		private:
			static void UpdateObject(UIObject* obj, float dt);
		};

	} // namespace ui
} // namespace aq
