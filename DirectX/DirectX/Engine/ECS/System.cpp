#include "System.h"
#include <thread>

namespace engine
{
	namespace ecs
	{
		SystemManager* SystemManager::instance_ = nullptr;


		void SystemManager::Update()
		{
			// TODO�F�����I�ɕ��񏈗���������
			for (auto&& system : systemList_) {
				system->Update();
			}
		}
	}
}