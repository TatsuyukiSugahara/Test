#include "aq.h"
#include "Prefab.h"
#include "HierarchicalTransformComponent.h"
#ifdef AQ_DEBUG_IMGUI
#include "ECS/EntityDebugTag.h"
#endif

namespace aq
{
	namespace ecs
	{
		Prefab& Prefab::AddChild(Prefab child)
		{
			children_.push_back(std::move(child));
			return *this;
		}


		Entity Prefab::Instantiate(const EntityHandle parentHandle) const
		{
			if (!factory_) return Entity();

			Entity entity = factory_();
			if (!entity.IsValid()) return entity;

#ifdef AQ_DEBUG_IMGUI
			if (auto* tag = entity.GetComponent<EntityDebugTag>())
				tag->SetName(name_.c_str());
#endif

			auto& ctx = EntityContext::Get();

			if (parentHandle.IsValid()) {
				ctx.SetParent(entity.GetHandle(), parentHandle);
			}

			const EntityHandle selfHandle = entity.GetHandle();
			for (const Prefab& child : children_) {
				child.Instantiate(selfHandle);
			}

			return entity;
		}
	}
}
