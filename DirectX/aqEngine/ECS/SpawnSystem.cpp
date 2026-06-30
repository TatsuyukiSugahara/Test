#include "aq.h"
#include "SpawnSystem.h"
#include "Engine.h"
#include "Prefab.h"
#include "Component/HierarchicalTransformComponent.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#endif

namespace aq
{
	namespace ecs
	{
		void SpawnSystem::Update()
		{
			const float dt = aq::Engine::GetDeltaTime();

			Foreach<SpawnerComponent, HierarchicalTransformComponent>(
				[dt](const Entity&, SpawnerComponent* spawner, HierarchicalTransformComponent* htc)
			{
				// 初回のみ path を解決（以降はキャッシュキーを再利用）。
				if (!spawner->resolved.IsValid() && !spawner->prefabPath.empty()) {
					spawner->resolved = PrefabRegistry::Get().Resolve(spawner->prefabPath);
				}
				if (!spawner->resolved.IsValid()) return;

				if (spawner->maxCount >= 0 && spawner->spawned >= spawner->maxCount) return;

				spawner->timer += dt;
				if (spawner->timer < spawner->interval) return;
				spawner->timer = 0.0f;

				// Find で shared_ptr を取得 → Instantiate が値捕獲する（設計 §4.3 寿命ルール）。
				if (auto data = PrefabRegistry::Get().Find(spawner->resolved)) {
					Prefab(data).Instantiate(htc->parentHandle);
					++spawner->spawned;
				}
			});
		}

#ifdef AQ_DEBUG_IMGUI
		void SpawnSystem::RenderContent()
		{
			int spawnerCount = 0;
			int totalSpawned = 0;
			Foreach<SpawnerComponent>([&](const Entity&, SpawnerComponent* spawner)
			{
				++spawnerCount;
				totalSpawned += spawner->spawned;
			});
			ImGui::Text("Spawners:      %d", spawnerCount);
			ImGui::Text("Total spawned: %d", totalSpawned);
		}
#endif
	}
}
