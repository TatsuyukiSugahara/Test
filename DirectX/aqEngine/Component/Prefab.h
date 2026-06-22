#pragma once
#include "ECS/ECS.h"
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace aq
{
	namespace ecs
	{
		// TC+HTC 同時生成の強制チェック用前方宣言
		struct TransformComponent;
		struct HierarchicalTransformComponent;

		// エンティティ生成テンプレート。
		// Prefab::Create<Components...>(name, setup) で定義し、Instantiate() でエンティティツリーを生成する。
		// AddChild() で子 Prefab を追加できる。生成時に HierarchicalTransformComponent があれば自動で親子設定される。
		// Foreach 内部からの呼び出しは不可（iterationMutex_ デッドロック）。初期化フェーズで使うこと。
		class Prefab
		{
		public:
			template <typename... Cs>
			static Prefab Create(
				std::string                    name,
				std::function<void(Entity)>    setup = nullptr)
			{
				static_assert(
					!(std::is_same_v<TransformComponent, Cs> || ...) ||
					 (std::is_same_v<HierarchicalTransformComponent, Cs> || ...),
					"TransformComponent を含む Prefab は HierarchicalTransformComponent も必須です");

				Prefab prefab;
				prefab.name_    = std::move(name);
				prefab.factory_ = [setup = std::move(setup)]() -> Entity
				{
					Entity entity = EntityContext::Get().CreateEntity<Cs...>();
					if (setup) setup(entity);
					return entity;
				};
				return prefab;
			}

			Prefab& AddChild(Prefab child);

			// エンティティツリーを生成する。
			// parentHandle が有効なら HierarchicalTransformComponent に親を設定する。
			Entity Instantiate(EntityHandle parentHandle = EntityHandle()) const;

			const std::string& GetName()     const { return name_; }
			bool               HasChildren() const { return !children_.empty(); }
			bool               IsValid()     const { return static_cast<bool>(factory_); }

		private:
			std::string                  name_;
			std::function<Entity()>      factory_;
			std::vector<Prefab>          children_;
		};
	}
}
