#include "System.h"
#include <thread>

namespace engine
{
	namespace ecs
	{
		SystemManager* SystemManager::instance_ = nullptr;


		void SystemManager::Update()
		{
			// TODOF«—ˆ“I‚É•À—ñˆ—‚³‚¹‚½‚¢
			for (auto&& system : systemList_) {
				system->Update();
			}
		}
	}
}