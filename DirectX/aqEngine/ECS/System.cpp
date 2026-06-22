#include "aq.h"
#include "System.h"
#include "Util/ThreadPool.h"
#include <queue>
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#endif


namespace aq
{
	namespace ecs
	{
		void SystemManager::Update()
		{
			EngineAssertMsg(registrationFinalized_,
				"SystemManager::Update: FinalizeRegistration has not been called");

			// wave ごとに全 task を submit してから全 future を get() する。
			// worker 内 future.wait() がなくなりスレッドを無駄に占有しない。
			// 例外は全 task 完了を待ってから最初の例外を rethrow する。
			const size_t maxLevel = systemEntries_.empty() ? 0
				: (*std::max_element(systemEntries_.begin(), systemEntries_.end(),
					[](const SystemEntry& a, const SystemEntry& b){ return a.level < b.level; })).level;

			for (size_t wave = 0; wave <= maxLevel; ++wave) {
				std::vector<std::future<void>> waveFutures;

				for (size_t idx : updateOrder_) {
					if (systemEntries_[idx].level != wave) continue;
					SystemBase* system = systemEntries_[idx].system.get();
					waveFutures.push_back(
						util::ThreadPool::Get().Submit([system]() { system->Update(); })
					);
				}

				std::exception_ptr firstException = nullptr;
				for (auto& fut : waveFutures) {
					try { fut.get(); }
					catch (...) {
						if (!firstException) firstException = std::current_exception();
					}
				}
				if (firstException) std::rethrow_exception(firstException);
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

			// 各 System の実行レベルを計算（level[i] = max(level[dep]) + 1）
			std::vector<size_t> levels(n, 0);
			for (size_t cur : updateOrder_) {
				for (size_t depIdx : systemEntries_[cur].dependencyIndices) {
					levels[cur] = std::max(levels[cur], levels[depIdx] + 1);
				}
				systemEntries_[cur].level = levels[cur];
			}

			registrationFinalized_ = true;

#ifdef AQ_DEBUG_IMGUI
			groups_.clear();
			for (auto& entry : systemEntries_)
			{
				const char* group = entry.system->GetDebugGroup();
				if (group == nullptr) continue;

				auto it = std::find_if(groups_.begin(), groups_.end(),
					[&](const GroupEntry& g) { return g.name == group; });
				if (it == groups_.end())
					groups_.push_back({ group, false, { entry.system.get() } });
				else
					it->systems.push_back(entry.system.get());
			}
#endif
		}


#ifdef AQ_DEBUG_IMGUI
		void SystemManager::DebugRenderMenuAll()
		{
			for (auto& entry : systemEntries_)
				if (entry.system->GetDebugGroup() == nullptr)
					entry.system->DebugRenderMenu();

			for (auto& g : groups_)
				ImGui::MenuItem(g.name.c_str(), nullptr, &g.show);
		}


		void SystemManager::DebugRenderAll()
		{
			for (auto& entry : systemEntries_)
				if (entry.system->GetDebugGroup() == nullptr)
					entry.system->DebugRender();

			for (auto& g : groups_)
			{
				if (!g.show) continue;
				if (ImGui::Begin(g.name.c_str()))
				{
					const std::string tabBarId = "##group_" + g.name;
					if (ImGui::BeginTabBar(tabBarId.c_str()))
					{
						for (auto* sys : g.systems)
						{
							if (ImGui::BeginTabItem(sys->GetDebugTabLabel()))
							{
								sys->RenderContent();
								ImGui::EndTabItem();
							}
						}
						ImGui::EndTabBar();
					}
				}
				ImGui::End();
			}
		}
#endif
	}
}
