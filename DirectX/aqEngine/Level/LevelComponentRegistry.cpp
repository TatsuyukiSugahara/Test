#include "aq.h"
#include "Level/LevelComponentRegistry.h"
#include "Level/LevelComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/EntityContext.h"
#include "ECS/JsonFieldVisitor.h"
#ifdef AQ_DEBUG_IMGUI
#include "ECS/ImGuiFieldVisitor.h"
#endif

namespace aq
{
	namespace level
	{
		void RegisterLevelComponents()
		{
			ecs::ComponentRegistry& registry = ecs::ComponentRegistry::Get();

			// --- LevelStreamComponent ---
			// SpawnerComponent と同じ流儀（typeName / serialize / deserialize / Inspector）。
			// 型消去エディタ（FillReflectPtrFns）対応は L7 で追加する。
			{
				ecs::ComponentMeta meta;
				meta.displayName = "Level Stream";
				meta.typeName    = "LevelStream";
				meta.has = [](ecs::EntityHandle h)          { return ecs::EntityContext::Get().GetComponent<LevelStreamComponent>(h) != nullptr; };
				meta.get = [](ecs::EntityHandle h) -> void* { return ecs::EntityContext::Get().GetComponent<LevelStreamComponent>(h); };
				meta.add = [](ecs::EntityHandle h)
				{
					auto& ctx = ecs::EntityContext::Get();
					if (!ctx.GetComponent<LevelStreamComponent>(h)) ctx.AddComponent<LevelStreamComponent>(h);
				};
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](ecs::EntityHandle h)
				{
					auto* comp = ecs::EntityContext::Get().GetComponent<LevelStreamComponent>(h);
					if (!comp) return;
					ecs::ImGuiFieldVisitor visitor;
					comp->Reflect(visitor);
				};
#endif
				meta.serialize = [](ecs::EntityHandle h, util::JsonValue& out)
				{
					auto* comp = ecs::EntityContext::Get().GetComponent<LevelStreamComponent>(h);
					if (!comp) return;
					ecs::JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](ecs::EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = ecs::EntityContext::Get().GetComponent<LevelStreamComponent>(h);
					if (!comp) return;
					ecs::JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
				};
				meta.remove = [](ecs::EntityHandle h) { ecs::EntityContext::Get().RemoveComponent<LevelStreamComponent>(h); };
				registry.Register(ecs::TypeInfo::Create<LevelStreamComponent>(), std::move(meta));
			}
		}
	}
}
