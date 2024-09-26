#include "System.h"
#include <thread>

namespace engine
{
	namespace ecs
	{
		SystemManager* SystemManager::instance_ = nullptr;


		void SystemManager::Update()
		{
			//std::vector<std::thread> threads;
			//for (uint32_t i = 0; i < systemList_.size(); ++i) {
			//	threads.push_back(std::thread([&] { systemList_[i]->Update(); }));
			//}

			//for (std::thread& t : threads) {
			//	t.join();
			//}


			// TODOF«—ˆ“I‚É•À—ñˆ—‚³‚¹‚½‚¢
			for (auto&& system : systemList_) {
				system->Update();
			}
		}
	}
}