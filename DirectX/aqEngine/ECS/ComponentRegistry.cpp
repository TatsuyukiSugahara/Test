#include "aq.h"
#include "ComponentRegistry.h"
#include "JsonFieldVisitor.h"
#include "EntityContext.h"
#include "Component/TerrainComponent.h"
#include "Component/OceanComponent.h"
#include "Component/DecalComponent.h"
#include "Component/InstancedStaticMeshComponentSystem.h"
#include "Component/InstancedPointListComponentSystem.h"
#include "Component/AnimationComponentSystem.h"
#include "Sound/Component/AudioSourceComponent.h"
#include "Sound/Component/AudioListenerComponent.h"
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
				meta.typeName     = "BoxStaticMesh";
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
				// 永続フィールドは無いが、Prefab の構成要素として追加・生成できるよう登録する（Reflect は空）。
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<BoxStaticMeshComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle, const util::JsonValue&) {};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<BoxStaticMeshComponent>(h); };
				FillReflectPtrFns<BoxStaticMeshComponent>(meta);
				registry.Register(TypeInfo::Create<BoxStaticMeshComponent>(), std::move(meta));
			}

			// --- InstancedStaticMeshComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Instanced Static Mesh";
				meta.typeName     = "InstancedStaticMesh";
				meta.requiredWith = {};
				meta.has  = [](EntityHandle h)         { return EntityContext::Get().GetComponent<InstancedStaticMeshComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<InstancedStaticMeshComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<InstancedStaticMeshComponent>(h))   ctx.AddComponent<InstancedStaticMeshComponent>(h);
				};
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<InstancedStaticMeshComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Reflect(visitor);
				};
#endif
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<InstancedStaticMeshComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<InstancedStaticMeshComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<InstancedStaticMeshComponent>(h); };
				FillReflectPtrFns<InstancedStaticMeshComponent>(meta);
				registry.Register(TypeInfo::Create<InstancedStaticMeshComponent>(), std::move(meta));
			}

			// --- InstancedPointListComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Instanced Point List";
				meta.typeName     = "InstancedPointList";
				meta.requiredWith = {};
				meta.has  = [](EntityHandle h)         { return EntityContext::Get().GetComponent<InstancedPointListComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<InstancedPointListComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<InstancedPointListComponent>(h))    ctx.AddComponent<InstancedPointListComponent>(h);
				};
#ifdef AQ_DEBUG_IMGUI
				// 座標を可変リストで編集する(各行 xyz 編集 + 行削除、末尾に追加、全消去)。
				// TODO: 別途 JSON など外部データから配置座標を読み込む対応を予定(手入力に加えて)。
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<InstancedPointListComponent>(h);
					if (!comp) return;

					float scale = comp->GetScale();
					if (ImGui::DragFloat("scale", &scale, 0.01f, 0.001f, 1000.0f)) { comp->SetScale(scale); }

					auto& points = comp->Points();
					ImGui::Text("points: %d", static_cast<int>(points.size()));

					int removeIndex = -1;
					for (int i = 0; i < static_cast<int>(points.size()); ++i)
					{
						ImGui::PushID(i);
						float xyz[3] = { points[i].x, points[i].y, points[i].z };
						if (ImGui::InputFloat3("##p", xyz)) {
							points[i].x = xyz[0];
							points[i].y = xyz[1];
							points[i].z = xyz[2];
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("X")) { removeIndex = i; }
						ImGui::PopID();
					}
					if (removeIndex >= 0) { points.erase(points.begin() + removeIndex); }

					if (ImGui::Button("Add point")) { comp->AddPoint(aq::math::Vector3(0.0f, 0.0f, 0.0f)); }
					ImGui::SameLine();
					if (ImGui::Button("Clear"))     { comp->Clear(); }
				};
#endif
				// 座標は動的なので永続化しない(serialize/deserialize は空)。
				meta.serialize   = [](EntityHandle, util::JsonValue&) {};
				meta.deserialize = [](EntityHandle, const util::JsonValue&) {};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<InstancedPointListComponent>(h); };
				FillReflectPtrFns<InstancedPointListComponent>(meta);
				registry.Register(TypeInfo::Create<InstancedPointListComponent>(), std::move(meta));
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
				meta.typeName     = "SkeletalMesh";
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
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<SkeletalMeshComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<SkeletalMeshComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
					comp->OnDeserialized();   // 読み込んだパスからメッシュをロード（副作用退避）
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<SkeletalMeshComponent>(h); };
				FillReflectPtrFns<SkeletalMeshComponent>(meta);
				registry.Register(TypeInfo::Create<SkeletalMeshComponent>(), std::move(meta));
			}

			// --- TerrainComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Terrain";
				meta.typeName     = "Terrain";
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
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<TerrainComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<TerrainComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
					comp->OnDeserialized();   // 読み込んだパス/パラメータで地形を再構築
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<TerrainComponent>(h); };
				FillReflectPtrFns<TerrainComponent>(meta);
				registry.Register(TypeInfo::Create<TerrainComponent>(), std::move(meta));
			}

			// --- OceanComponent ---
			{
				ComponentMeta meta;
				meta.displayName  = "Ocean";
				meta.typeName     = "Ocean";
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
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<OceanComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<OceanComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
					comp->OnDeserialized();   // 読み込んだ params で海を再構築
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<OceanComponent>(h); };
				FillReflectPtrFns<OceanComponent>(meta);
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

			// --- AnimationComponent（複数クリップ = collection。専用シリアライズ）---
			{
				ComponentMeta meta;
				meta.displayName  = "Animation";
				meta.typeName     = "Animation";
				meta.requiredWith = { TypeInfo::Create<SkeletalMeshComponent>() };
				meta.has  = [](EntityHandle h)          { return EntityContext::Get().GetComponent<AnimationComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<AnimationComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<SkeletalMeshComponent>(h))          ctx.AddComponent<SkeletalMeshComponent>(h);
					if (!ctx.GetComponent<AnimationComponent>(h))             ctx.AddComponent<AnimationComponent>(h);
				};
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector    = [](EntityHandle h)
				{
					auto* c = EntityContext::Get().GetComponent<AnimationComponent>(h);
					if (!c) return;
					// 同居する SkeletalMesh のモデルパスを渡し、.fbx ならクリップ候補を表示させる。
					auto* sk = EntityContext::Get().GetComponent<SkeletalMeshComponent>(h);
					c->DrawInspectorImGui(sk ? sk->GetModelPath() : std::string());
				};
				meta.drawInspectorPtr = [](void* p)        { static_cast<AnimationComponent*>(p)->DrawInspectorImGui(); };
#endif
				// collection のため Reflect ビジターではなく専用の SerializeTo/DeserializeFrom を使う。
				meta.serialize      = [](EntityHandle h, util::JsonValue& out)       { auto* c = EntityContext::Get().GetComponent<AnimationComponent>(h); if (c) c->SerializeTo(out); };
				meta.deserialize    = [](EntityHandle h, const util::JsonValue& in)  { auto* c = EntityContext::Get().GetComponent<AnimationComponent>(h); if (c) c->DeserializeFrom(in); };
				meta.serializePtr   = [](void* p, util::JsonValue& out)              { static_cast<AnimationComponent*>(p)->SerializeTo(out); };
				meta.deserializePtr = [](void* p, const util::JsonValue& in)         { static_cast<AnimationComponent*>(p)->DeserializeFrom(in); };
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<AnimationComponent>(h); };
				registry.Register(TypeInfo::Create<AnimationComponent>(), std::move(meta));
			}

			// --- AudioSourceComponent（3D 発音体）---
			{
				ComponentMeta meta;
				meta.displayName  = "Audio Source";
				meta.typeName     = "AudioSource";
				meta.requiredWith = { TypeInfo::Create<TransformComponent>(), TypeInfo::Create<HierarchicalTransformComponent>() };
				meta.has  = [](EntityHandle h)          { return EntityContext::Get().GetComponent<sound::AudioSourceComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<sound::AudioSourceComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<sound::AudioSourceComponent>(h))    ctx.AddComponent<sound::AudioSourceComponent>(h);
				};
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<sound::AudioSourceComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Reflect(visitor);
				};
#endif
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<sound::AudioSourceComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle h, const util::JsonValue& in)
				{
					auto* comp = EntityContext::Get().GetComponent<sound::AudioSourceComponent>(h);
					if (!comp) return;
					JsonReadVisitor visitor(in);
					comp->Reflect(visitor);
					comp->OnDeserialized();   // clipPath からクリップをロード
				};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<sound::AudioSourceComponent>(h); };
				FillReflectPtrFns<sound::AudioSourceComponent>(meta);
				registry.Register(TypeInfo::Create<sound::AudioSourceComponent>(), std::move(meta));
			}

			// --- AudioListenerComponent（リスナーマーカー・永続データなし）---
			{
				ComponentMeta meta;
				meta.displayName  = "Audio Listener";
				meta.typeName     = "AudioListener";
				meta.requiredWith = { TypeInfo::Create<TransformComponent>(), TypeInfo::Create<HierarchicalTransformComponent>() };
				meta.has  = [](EntityHandle h)          { return EntityContext::Get().GetComponent<sound::AudioListenerComponent>(h) != nullptr; };
				meta.get  = [](EntityHandle h) -> void* { return EntityContext::Get().GetComponent<sound::AudioListenerComponent>(h); };
				meta.add  = [](EntityHandle h)
				{
					auto& ctx = EntityContext::Get();
					if (!ctx.GetComponent<TransformComponent>(h))             ctx.AddComponent<TransformComponent>(h);
					if (!ctx.GetComponent<HierarchicalTransformComponent>(h)) ctx.AddComponent<HierarchicalTransformComponent>(h);
					if (!ctx.GetComponent<sound::AudioListenerComponent>(h))  ctx.AddComponent<sound::AudioListenerComponent>(h);
				};
#ifdef AQ_DEBUG_IMGUI
				meta.drawInspector = [](EntityHandle h)
				{
					auto* comp = EntityContext::Get().GetComponent<sound::AudioListenerComponent>(h);
					if (!comp) return;
					ImGuiFieldVisitor visitor;
					comp->Reflect(visitor);
				};
#endif
				meta.serialize = [](EntityHandle h, util::JsonValue& out)
				{
					auto* comp = EntityContext::Get().GetComponent<sound::AudioListenerComponent>(h);
					if (!comp) return;
					JsonWriteVisitor visitor;
					comp->Reflect(visitor);
					out = std::move(visitor.obj);
				};
				meta.deserialize = [](EntityHandle, const util::JsonValue&) {};
				meta.remove = [](EntityHandle h) { EntityContext::Get().RemoveComponent<sound::AudioListenerComponent>(h); };
				FillReflectPtrFns<sound::AudioListenerComponent>(meta);
				registry.Register(TypeInfo::Create<sound::AudioListenerComponent>(), std::move(meta));
			}
		}
	}
}
