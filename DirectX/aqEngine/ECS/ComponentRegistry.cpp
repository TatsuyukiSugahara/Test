#include "aq.h"
#include "ComponentRegistry.h"
#include "JsonFieldVisitor.h"
#include "EntityContext.h"
#include "Component/TransformComponentSystem.h"
#include "Component/HierarchicalTransformComponent.h"
#include "Component/BodyComponentSystem.h"
#include "Component/TerrainComponent.h"
#include "Component/OceanComponent.h"
#include "Component/DecalComponent.h"
#include "SpawnSystem.h"
#ifdef AQ_DEBUG_IMGUI
#include "ImGuiFieldVisitor.h"   // drawInspector（ImGui 編集）専用。serialize/deserialize は非依存。
#endif

// レジストリのコア（typeName / serialize / deserialize / add / has / get / requiredWith）は
// 常時コンパイルする。これによりリリースビルド（AQ_DEBUG_IMGUI 無効）でも JSON から
// コンポーネントを復元できる。ImGui で編集する drawInspector のみ #ifdef AQ_DEBUG_IMGUI で囲む。

namespace aq
{
	namespace ecs
	{
		namespace
		{
			// Reflect 化済みコンポーネント T に対し、型消去 void* 版の serialize/deserialize/drawInspector を
			// 一括設定する（Prefab エディタ用・設計 §6.2/§6.3）。Reflect を単一の真実として共有する。
			template <typename T>
			void FillReflectPtrFns(ComponentMeta& meta)
			{
				meta.serializePtr = [](void* p, util::JsonValue& out)
				{
					JsonWriteVisitor visitor;
					static_cast<T*>(p)->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserializePtr = [](void* p, const util::JsonValue& in)
				{
					JsonReadVisitor visitor(in);
					static_cast<T*>(p)->Reflect(visitor);
				};
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspectorPtr = [](void* p)
				{
					ImGuiFieldVisitor visitor;
					static_cast<T*>(p)->Reflect(visitor);
				};
#endif
			}
		}


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


		const ComponentMeta* ComponentRegistry::Find(std::string_view typeName) const
		{
			for (const auto& entry : entries_) {
				if (entry.second.typeName && typeName == entry.second.typeName) return &entry.second;
			}
			return nullptr;
		}


		TypeInfo ComponentRegistry::TypeOf(std::string_view typeName) const
		{
			for (const auto& entry : entries_) {
				if (entry.second.typeName && typeName == entry.second.typeName) return entry.first;
			}
			return TypeInfo();   // GetHash() == size_t(-1) → 未解決（TypeInfo() と == で判定）
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
				meta.typeName     = "Transform";
				meta.requiredWith = { TypeInfo::Create<HierarchicalTransformComponent>() };
				meta.has  = [](EntityHandle h)         { return EntityContext::Get().GetComponent<TransformComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<TransformComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
				};
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<TransformComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Reflect(visitor);
				};
#endif
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<TransformComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<TransformComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
				};
				meta.remove = nullptr;  // TC+HTC はペアで必須のため UI からは除去不可
				FillReflectPtrFns<TransformComponent>(meta);
				registry.Register(TypeInfo::Create<TransformComponent>(), std::move(meta));
			}

			// --- HierarchicalTransformComponent ---
			// 親子/ワールド座標は runtime/構造由来のため serialize 対象外（階層は Prefab ツリーが表現）。
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
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<HierarchicalTransformComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
#endif
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
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<BoxStaticMeshComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
#endif
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<BoxStaticMeshComponent>(h); };
				registry.Register(TypeInfo::Create<BoxStaticMeshComponent>(), std::move(meta));
			}

			// --- StaticMeshComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Static Mesh";
				meta.typeName     = "StaticMesh";
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
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<StaticMeshComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
#endif
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<StaticMeshComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<StaticMeshComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
					comp->OnDeserialized();   // 読み込んだパスからメッシュをロード（副作用退避）
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<StaticMeshComponent>(h); };
				FillReflectPtrFns<StaticMeshComponent>(meta);
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
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<SkeletalMeshComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
#endif
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<SkeletalMeshComponent>(h); };
				registry.Register(TypeInfo::Create<SkeletalMeshComponent>(), std::move(meta));
			}

			// --- TerrainComponent ---
			// 注: Inspect が ImGui 直呼び（HeightScale 等）を含むため Reflect 化は未対応（serialize なし）。
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
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<TerrainComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
#endif
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<TerrainComponent>(h); };
				registry.Register(TypeInfo::Create<TerrainComponent>(), std::move(meta));
			}

			// --- OceanComponent ---
			// 注: Inspect が ImGui 直呼びのため Reflect 化は未対応（serialize なし）。
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
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<OceanComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Inspect(visitor);
				};
#endif
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<OceanComponent>(h); };
				registry.Register(TypeInfo::Create<OceanComponent>(), std::move(meta));
			}

			// --- DecalComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Decal";
				meta.typeName     = "Decal";
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
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<DecalComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Reflect(visitor);
				};
#endif
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<DecalComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<DecalComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<DecalComponent>(h); };
				FillReflectPtrFns<DecalComponent>(meta);
				registry.Register(TypeInfo::Create<DecalComponent>(), std::move(meta));
			}

			// --- PrefabReferenceComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Prefab Reference";
				meta.typeName     = "PrefabReference";
				meta.requiredWith = {};
				meta.has  = [](EntityHandle h)          { return EntityContext::Get().GetComponent<PrefabReferenceComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<PrefabReferenceComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<PrefabReferenceComponent>(h)) ctx.AddComponent<PrefabReferenceComponent>(h);
				};
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<PrefabReferenceComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Reflect(visitor);
				};
#endif
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<PrefabReferenceComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<PrefabReferenceComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<PrefabReferenceComponent>(h); };
				FillReflectPtrFns<PrefabReferenceComponent>(meta);
				registry.Register(TypeInfo::Create<PrefabReferenceComponent>(), std::move(meta));
			}

			// --- SpawnerComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Spawner";
				meta.typeName     = "Spawner";
				meta.requiredWith = { TypeInfo::Create<TransformComponent>(), TypeInfo::Create<HierarchicalTransformComponent>() };
				meta.has  = [](EntityHandle h)          { return EntityContext::Get().GetComponent<SpawnerComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<SpawnerComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<SpawnerComponent>(h))               ctx.AddComponent<SpawnerComponent>(h);
				};
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<SpawnerComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Reflect(visitor);
				};
#endif
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<SpawnerComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<SpawnerComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<SpawnerComponent>(h); };
				FillReflectPtrFns<SpawnerComponent>(meta);
				registry.Register(TypeInfo::Create<SpawnerComponent>(), std::move(meta));
			}
		}
	}
}
