#include "JobSystem.h"


namespace engine
{


	JobSystem* JobSystem::instance_ = nullptr;


	JobSystem::JobSystem()
		: jobCounter_(0)
		, completedFlag_(false)
		, stopFlag_(false)
	{
	}


	JobSystem::~JobSystem()
	{
		// 全てのワーカースレッドを停止させる
		stopFlag_.store(true);

		// 全てのワーカースレッドを起こす
		condition_.notify_all();

		// 全てのワーカースレッドが終了するのを待つ
		for (auto& worker : workers_) {
			if (worker.joinable()) {
				worker.join();
			}
		}
	}
}