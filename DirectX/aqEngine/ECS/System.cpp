#include "aq.h"
#include "System.h"
#include "Util/ThreadPool.h"
#include <queue>


namespace aq
{
	namespace ecs
	{
		void SystemManager::Update()
		{
			EngineAssertMsg(registrationFinalized_,
				"SystemManager::Update: FinalizeRegistration has not been called");

			const size_t count = systemEntries_.size();
			std::vector<std::shared_future<void>> futures(count);

			for (size_t idx : updateOrder_) {
				SystemBase* system = systemEntries_[idx].system.get();

				std::vector<std::shared_future<void>> deps;
				deps.reserve(systemEntries_[idx].dependencyIndices.size());
				for (size_t depIdx : systemEntries_[idx].dependencyIndices) {
					deps.push_back(futures[depIdx]);
				}

				futures[idx] = util::ThreadPool::Get().Submit(
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


		void SystemManager::BuildSchedule()
		{
			const size_t n = systemEntries_.size();
			updateOrder_.clear();
			updateOrder_.reserve(n);

			// Kahn's algorithm によるトポロジカルソート
			// dependencyIndices[i] = i が依存するシステムのインデックス
			// edge: dep → i (dep は i より前に実行)

			std::vector<size_t> inDegree(n, 0);
			std::vector<std::vector<size_t>> dependents(n);

			for (size_t i = 0; i < n; ++i) {
				inDegree[i] = systemEntries_[i].dependencyIndices.size();
				for (size_t depIdx : systemEntries_[i].dependencyIndices) {
					dependents[depIdx].push_back(i);
				}
			}

			std::queue<size_t> ready;
			for (size_t i = 0; i < n; ++i) {
				if (inDegree[i] == 0)
					ready.push(i);
			}

			while (!ready.empty()) {
				const size_t cur = ready.front();
				ready.pop();
				updateOrder_.push_back(cur);

				for (size_t next : dependents[cur]) {
					if (--inDegree[next] == 0)
						ready.push(next);
				}
			}

			EngineAssertMsg(updateOrder_.size() == n,
				"SystemManager::BuildSchedule: circular dependency detected");

			registrationFinalized_ = true;
		}
	}
}
