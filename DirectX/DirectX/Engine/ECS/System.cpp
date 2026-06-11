#include "System.h"
#include "../Util/ThreadPool.h"


namespace engine
{
	namespace ecs
	{
		SystemManager* SystemManager::instance_ = nullptr;


		void SystemManager::Update()
		{
			const size_t count = systemEntries_.size();
			std::vector<std::shared_future<void>> futures(count);

			for (size_t i = 0; i < count; ++i) {
				SystemBase* system = systemEntries_[i].system.get();

				// 依存するSystemのfutureをコピー(shared_futureなので複数から待てる)
				std::vector<std::shared_future<void>> deps;
				deps.reserve(systemEntries_[i].dependencyIndices.size());
				for (size_t depIdx : systemEntries_[i].dependencyIndices) {
					deps.push_back(futures[depIdx]);
				}

				futures[i] = util::ThreadPool::Get().Submit(
					[system, deps = std::move(deps)]() {
						for (const auto& dep : deps) {
							dep.wait();
						}
						system->Update();
					}
				).share();
			}

			for (auto& fut : futures) {
				if (fut.valid()) {
					fut.wait();
				}
			}
		}
	}
}
