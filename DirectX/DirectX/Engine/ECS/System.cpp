#include "System.h"
#include <thread>

namespace engine
{
	namespace ecs
	{
		SystemManager* SystemManager::instance_ = nullptr;


		void SystemManager::Update()
		{
			// TODO：将来的に並列処理させたい
			for (auto&& system : systemList_) {
				system->Update();
			}
		}
	}
}