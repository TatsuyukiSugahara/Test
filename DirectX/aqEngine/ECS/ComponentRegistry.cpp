#include "aq.h"
#ifdef AQ_DEBUG_IMGUI
#include "ComponentRegistry.h"
#include "ImGuiFieldVisitor.h"
#include "EntityContext.h"
#include "Component/TransformComponentSystem.h"
#include "Component/HierarchicalTransformComponent.h"
#include "Component/BodyComponentSystem.h"
#include "Component/TerrainComponent.h"
#include "Component/OceanComponent.h"
#include "Component/DecalComponent.h"

namespace aq
{
	namespace ecs
	{
		ComponentRegistry& ComponentRegistry::Get()
		{
			static ComponentRegistry instance;
			return instance;
		}


		void ComponentRegistry::Register(TypeInfo typeInfo, ComponentMeta meta)
		{
			for (const auto& entry : entries_) {
				if (entry.first == typeInfo) return;
			}
			entries_.emplace_back(typeInfo, std::move(meta));
		}


		const ComponentMeta* ComponentRegistry::Find(size_t typeHash) const
		{
			for (const auto& entry : entries_) {
				if (entry.first.GetHash() == typeHash) return &entry.second;
			}
			return nullptr;
		}


		void ComponentRegistry::RegisterCoreComponents()
		{
			ComponentRegistry& registry = Get();
			if (registry.coreRegistered_) return;
			registry.coreRegistered_ = true;

			// --- TransformComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Transform";
				meta.requiredWith = { TypeInfo::Create<HierarchicalTransformComponent>() };
				meta.has  = [](EntityHandle h)         { return EntityContext::Get().GetComponent<TransformComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<TransformComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
				};
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<TransformComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
				meta.remove = nullptr;  // TC+HTC はペアで必須のため UI からは除去不可
				registry.Register(TypeInfo::Create<TransformComponent>(), std::move(meta));
			}

			// --- HierarchicalTransformComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Hierarchical Transform";
				meta.requiredWith = { TypeInfo::Create<TransformComponent>() };
				meta.has  = [](EntityHandle h)         { return EntityContext::Get().GetComponent<HierarchicalTransformComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<HierarchicalTransformComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
				};
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<HierarchicalTransformComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
				meta.remove = nullptr;  // TC+HTC はペアで必須のため UI からは除去不可
				registry.Register(TypeInfo::Create<HierarchicalTransformComponent>(), std::move(meta));
			}

			// --- BoxStaticMeshComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Box Static Mesh";
				meta.requiredWith = {};
				meta.has  = [](EntityHandle h)         { return EntityContext::Get().GetComponent<BoxStaticMeshComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<BoxStaticMeshComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<BoxStaticMeshComponent>(h))         ctx.AddComponent<BoxStaticMeshComponent>(h);
				};
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<BoxStaticMeshComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<BoxStaticMeshComponent>(h); };
				registry.Register(TypeInfo::Create<BoxStaticMeshComponent>(), std::move(meta));
			}

			// --- StaticMeshComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Static Mesh";
				meta.requiredWith = {};
				meta.has  = [](EntityHandle h)         { return EntityContext::Get().GetComponent<StaticMeshComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<StaticMeshComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<StaticMeshComponent>(h))            ctx.AddComponent<StaticMeshComponent>(h);
				};
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<StaticMeshComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<StaticMeshComponent>(h); };
				registry.Register(TypeInfo::Create<StaticMeshComponent>(), std::move(meta));
			}

			// --- SkeletalMeshComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Skeletal Mesh";
				meta.requiredWith = {};
				meta.has  = [](EntityHandle h)         { return EntityContext::Get().GetComponent<SkeletalMeshComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<SkeletalMeshComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<SkeletalMeshComponent>(h))          ctx.AddComponent<SkeletalMeshComponent>(h);
				};
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<SkeletalMeshComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<SkeletalMeshComponent>(h); };
				registry.Register(TypeInfo::Create<SkeletalMeshComponent>(), std::move(meta));
			}

			// --- TerrainComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Terrain";
				meta.requiredWith = {};
				meta.has  = [](EntityHandle h)          { return EntityContext::Get().GetComponent<TerrainComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<TerrainComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<TerrainComponent>(h))               ctx.AddComponent<TerrainComponent>(h);
				};
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<TerrainComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<TerrainComponent>(h); };
				registry.Register(TypeInfo::Create<TerrainComponent>(), std::move(meta));
			}

			// --- OceanComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Ocean";
				meta.requiredWith = {};
				meta.has  = [](EntityHandle h)          { return EntityContext::Get().GetComponent<OceanComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<OceanComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<OceanComponent>(h))                 ctx.AddComponent<OceanComponent>(h);
				};
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<OceanComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<OceanComponent>(h); };
				registry.Register(TypeInfo::Create<OceanComponent>(), std::move(meta));
			}

			// --- DecalComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Decal";
				meta.requiredWith = {};
				meta.has  = [](EntityHandle h)          { return EntityContext::Get().GetComponent<DecalComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<DecalComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<DecalComponent>(h))                 ctx.AddComponent<DecalComponent>(h);
				};
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<DecalComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<DecalComponent>(h); };
				registry.Register(TypeInfo::Create<DecalComponent>(), std::move(meta));
			}
		}
	}
}
#endif
