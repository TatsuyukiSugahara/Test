#include "aq.h"
#include "Prefab.h"
#include "EntityContext.h"
#include "ComponentRegistry.h"
#ifdef AQ_DEBUG_IMGUI
#include "EntityDebugTag.h"
#endif

namespace aq
{
	namespace ecs
	{
		namespace
		{
			// ノードの components キー（typeName）→ TypeInfo 列を作る。
			// requiredWith を推移的に展開する（registry 依存のため Prefab 層の責務・設計 §4.3）。
			// dedup と上限診断は生成プリミティブ（CreateEntityFromTypesNoLock）側で行うためここでは不要。
			std::vector<TypeInfo> CollectTypes(const util::JsonValue& components)
			{
				const ComponentRegistry& registry = ComponentRegistry::Get();
				std::vector<TypeInfo> types;

				// 1) 明示的に列挙されたコンポーネント
				for (const auto& kv : components.GetObject())
				{
					const TypeInfo t = registry.TypeOf(kv.first);
					if (t == TypeInfo())   // 未解決（typeName 未登録）
					{
						EngineAssertMsg(false,
							"Prefab::Instantiate: unknown component typeName in prefab JSON");
						continue;
					}
					types.push_back(t);
				}

				// 1.5) TransformComponent + HierarchicalTransformComponent は全 Prefab の必須基底。
				//      Transform を必ず含める（HTC は下の requiredWith 展開で自動追加される）。
				{
					const TypeInfo tc = registry.TypeOf("Transform");
					if (tc != TypeInfo())
					{
						bool present = false;
						for (const TypeInfo& t : types) {
							if (t == tc) { present = true; break; }
						}
						if (!present) types.push_back(tc);
					}
				}

				// 2) requiredWith の推移展開（固定点まで）。
				bool changed = true;
				while (changed)
				{
					changed = false;
					for (size_t i = 0; i < types.size(); ++i)
					{
						const ComponentMeta* meta = registry.Find(types[i].GetHash());
						if (!meta) continue;
						for (const TypeInfo& req : meta->requiredWith)
						{
							bool present = false;
							for (const TypeInfo& have : types) {
								if (have == req) { present = true; break; }
							}
							if (!present) { types.push_back(req); changed = true; }
						}
					}
				}
				return types;
			}


			// 1 ノードを生成して deserialize + 親子付けし、子を再帰生成する。
			// create はノードの完全な TypeInfo 列から Entity を作る生成プリミティブ
			// （遅延=NoLock / 即時=ロック版 の両方に対応）。失敗時は無効な Entity を返す。
			Entity InstantiateNode(
				const PrefabNodeData&                                     node,
				EntityHandle                                              parent,
				const EntityManager::DeferredCreateFn&                    create,
				const std::function<void(Entity, const PrefabNodeData&)>& onEachCreated)
			{
				// 1 ノードを生成（子は再帰しない共有プリミティブへ委譲）。
				Entity entity = InstantiatePrefabNode(node, parent, create, onEachCreated);
				if (!entity.IsValid()) return entity;

				const EntityHandle self = entity.GetHandle();
				for (const PrefabNodeData& child : node.children) {
					InstantiateNode(child, self, create, onEachCreated);
				}
				return entity;
			}
		}


		Entity InstantiatePrefabNode(
			const PrefabNodeData&                                     node,
			EntityHandle                                              parent,
			const std::function<Entity(std::vector<TypeInfo>)>&       create,
			const std::function<void(Entity, const PrefabNodeData&)>& onEachCreated)
		{
			Entity entity = create(CollectTypes(node.components));
			if (!entity.IsValid()) return entity;

			EntityContext& ctx = EntityContext::Get();
			const EntityHandle self = entity.GetHandle();

			// 各コンポーネントを JsonValue から復元する。
			const ComponentRegistry& registry = ComponentRegistry::Get();
			for (const auto& kv : node.components.GetObject())
			{
				const ComponentMeta* meta = registry.Find(kv.first);
				if (meta && meta->deserialize) {
					meta->deserialize(self, kv.second);
				}
			}

#ifdef AQ_DEBUG_IMGUI
			if (auto* tag = entity.GetComponent<EntityDebugTag>()) {
				tag->SetName(node.name.c_str());
			}
#endif

			// 生成・deserialize 完了直後のフック（Level 層が levelId を差す等）。
			if (onEachCreated) onEachCreated(entity, node);

			if (parent.IsValid()) {
				ctx.SetParent(self, parent);
			}
			return entity;
		}


		Entity InstantiatePrefabTree(
			const PrefabNodeData&                                     root,
			EntityHandle                                              parent,
			const std::function<Entity(std::vector<TypeInfo>)>&       create,
			const std::function<void(Entity, const PrefabNodeData&)>& onEachCreated)
		{
			return InstantiateNode(root, parent, create, onEachCreated);
		}


		void Prefab::Instantiate(const EntityHandle parent, std::function<void(Entity)> onComplete) const
		{
			if (!data_) return;

			// 設計 §4.3: this/参照ではなく shared_ptr<const PrefabData> を値捕獲する。
			// これにより一時 Prefab・Registry アンロード後の Flush でも生存が保証される。
			std::shared_ptr<const PrefabData> data = data_;
			EntityContext::Get().RequestDeferredBuild(
				[data, parent, onComplete = std::move(onComplete)]
				(const EntityManager::DeferredCreateFn& create)
				{
					Entity root = InstantiateNode(data->root, parent, create, nullptr);
					if (onComplete && root.IsValid()) onComplete(root);
				});
		}


		Entity Prefab::InstantiateImmediate(const EntityHandle parent) const
		{
			if (!data_) return Entity();

			EntityManager::DeferredCreateFn create =
				[](std::vector<TypeInfo> types) -> Entity
				{
					return EntityContext::Get().CreateEntityFromTypes(std::move(types));
				};
			return InstantiateNode(data_->root, parent, create, nullptr);
		}
	}
}
