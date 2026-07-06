#include "aq.h"
#include "Level/LevelStreamSystem.h"
#include "Level/LevelComponents.h"
#include "Level/LevelManager.h"
#include "ECS/ECS.h"            // ecs::Foreach
#include "ECS/EntityContext.h"
#include <vector>
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#endif

namespace aq
{
	namespace level
	{
		void LevelStreamSystem::Update()
		{
			LevelManager&       levelManager = LevelManager::Get();
			ecs::EntityContext& ctx          = ecs::EntityContext::Get();

			// Load/Unload は内部で別の Foreach を回す（ネスト走査を避ける）ため、
			// まず対象 Entity を収集してから Foreach の外で実行する。
			std::vector<ecs::EntityHandle> toLoad;
			std::vector<ecs::EntityHandle> toUnload;

			ecs::Foreach<LevelStreamComponent>(
				[&](const ecs::Entity& entity, LevelStreamComponent* stream)
				{
					if (stream->levelPath.empty()) return;
					const bool loaded = levelManager.IsLoaded(stream->loaded);
					if (stream->loadWhenActive && !loaded)      toLoad.push_back(entity.GetHandle());
					else if (!stream->loadWhenActive && loaded) toUnload.push_back(entity.GetHandle());
				});

			for (const ecs::EntityHandle handle : toLoad)
			{
				auto* stream = ctx.GetComponent<LevelStreamComponent>(handle);
				if (stream && !stream->levelPath.empty() && !levelManager.IsLoaded(stream->loaded))
					stream->loaded = levelManager.Load(stream->levelPath);
			}

			for (const ecs::EntityHandle handle : toUnload)
			{
				auto* stream = ctx.GetComponent<LevelStreamComponent>(handle);
				if (stream && levelManager.IsLoaded(stream->loaded))
				{
					levelManager.Unload(stream->loaded);
					stream->loaded = LevelId();
				}
			}
		}


#ifdef AQ_DEBUG_IMGUI
		void LevelStreamSystem::RenderContent()
		{
			int total  = 0;
			int loaded = 0;
			ecs::Foreach<LevelStreamComponent>(
				[&](const ecs::Entity&, LevelStreamComponent* stream)
				{
					++total;
					if (LevelManager::Get().IsLoaded(stream->loaded)) ++loaded;
				});
			ImGui::Text("Streamers: %d", total);
			ImGui::Text("Loaded:    %d", loaded);
		}
#endif
	}
}
