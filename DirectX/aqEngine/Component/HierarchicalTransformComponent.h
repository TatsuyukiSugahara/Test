#pragma once
#include "ECS/ECS.h"
#include "TransformComponentSystem.h"
#include <vector>

namespace aq
{
	namespace ecs
	{
		// 親子階層とワールド座標を管理するコンポーネント。
		// TransformComponent (ローカル) と組み合わせて使う。
		struct HierarchicalTransformComponent : public IComponent
		{
			ecsComponent(aq::ecs::HierarchicalTransformComponent);

			Transform                 transform;           // ワールド座標
			EntityHandle              parentHandle;
			std::vector<EntityHandle> childHandles;
			uint64_t                  lastUpdatedFrame = 0;
			bool                      resolving        = false;

			// 親を設定する。循環参照 / self 参照 / invalid は false を返す。
			// 旧親の childHandles から除去し、新親に追加する。
			bool SetParent(EntityHandle self, EntityHandle parent);

			// 親との関係を解除する。旧親の childHandles から self を除去する。
			void DetachParent(EntityHandle self);

#ifdef AQ_DEBUG_IMGUI
			template <typename V>
			void Inspect(V& visitor)
			{
				// ワールド座標はシステムが計算するため読み取り専用
				visitor.ReadOnly("position (world)", transform.position);
				visitor.ReadOnly("scale    (world)", transform.scale);
				visitor.ReadOnly("rotation (world)", transform.rotation);
				visitor.ReadOnly("children",         static_cast<int>(childHandles.size()));
			}
#endif
		};


		// HierarchicalTransformComponent を持つエンティティのワールド座標を更新する System。
		class HierarcicalTransformSystem : public SystemBase
		{
		public:
			HierarcicalTransformSystem();
			~HierarcicalTransformSystem();

			void Update() override;

			static HierarcicalTransformSystem& Get()      { return *instance_; }
			static bool                        IsAvailable() { return instance_ != nullptr; }


		private:
			static void UpdateWorld(
				EntityHandle                    self,
				TransformComponent*             transformComponent,
				HierarchicalTransformComponent* hierarchicalTransformComponent,
				uint64_t                        frameId);

			uint64_t updateFrameId_ = 0;
			static HierarcicalTransformSystem* instance_;
		};
	}
}
