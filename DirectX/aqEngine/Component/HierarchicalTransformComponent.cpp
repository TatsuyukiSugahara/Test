#include "aq.h"
#include "Engine.h"
#include "HierarchicalTransformComponent.h"
#include "ECS/EntityContext.h"
#include <algorithm>

namespace aq
{
	namespace ecs
	{
		bool HierarchicalTransformComponent::SetParent(const EntityHandle self, const EntityHandle parent)
		{
			auto& ctx = EntityContext::Get();
			if (!ctx.IsValid(parent)) return false;
			if (self == parent)       return false;

			// 親は TransformComponent + HierarchicalTransformComponent 必須
			if (!ctx.GetComponent<TransformComponent>(parent))             return false;
			auto* newParent = ctx.GetComponent<HierarchicalTransformComponent>(parent);
			if (!newParent) return false;

			// 祖先チェーンに self が含まれないか確認
			EntityHandle current = parent;
			constexpr int MAX_DEPTH = 64;
			for (int depth = 0; depth < MAX_DEPTH; ++depth) {
				if (!ctx.IsValid(current)) break;
				const auto* ancestor = ctx.GetComponent<HierarchicalTransformComponent>(current);
				if (!ancestor) break;
				if (ancestor->parentHandle == self) return false;
				if (!ctx.IsValid(ancestor->parentHandle)) break;
				current = ancestor->parentHandle;
				if (depth == MAX_DEPTH - 1) return false;
			}

			// 旧親の childHandles から除去
			if (ctx.IsValid(parentHandle)) {
				auto* oldParent = ctx.GetComponent<HierarchicalTransformComponent>(parentHandle);
				if (oldParent) {
					auto& oldChildren = oldParent->childHandles;
					oldChildren.erase(std::remove(oldChildren.begin(), oldChildren.end(), self), oldChildren.end());
				}
			}

			// 新親の childHandles に追加
			newParent->childHandles.push_back(self);
			parentHandle = parent;
			return true;
		}


		void HierarchicalTransformComponent::DetachParent(const EntityHandle self)
		{
			auto& ctx = EntityContext::Get();
			if (ctx.IsValid(parentHandle)) {
				auto* oldParent = ctx.GetComponent<HierarchicalTransformComponent>(parentHandle);
				if (oldParent) {
					auto& oldChildren = oldParent->childHandles;
					oldChildren.erase(std::remove(oldChildren.begin(), oldChildren.end(), self), oldChildren.end());
				}
			}
			parentHandle = EntityHandle();
		}


		/*******************************************/


		HierarcicalTransformSystem* HierarcicalTransformSystem::instance_ = nullptr;


		HierarcicalTransformSystem::HierarcicalTransformSystem()
		{
			instance_ = this;
		}


		HierarcicalTransformSystem::~HierarcicalTransformSystem()
		{
			instance_ = nullptr;
		}


		void HierarcicalTransformSystem::Update()
		{
			++updateFrameId_;
			const uint64_t frameId = updateFrameId_;

			aq::ecs::Foreach<TransformComponent, HierarchicalTransformComponent>(
				[frameId](const aq::ecs::Entity& entity, TransformComponent* transformComponent, HierarchicalTransformComponent* hierarchicalTransformComponent)
				{
					UpdateWorld(entity.GetHandle(), transformComponent, hierarchicalTransformComponent, frameId);
				});
		}


		void HierarcicalTransformSystem::UpdateWorld(
			const EntityHandle              self,
			TransformComponent*             transformComponent,
			HierarchicalTransformComponent* hierarchicalTransformComponent,
			const uint64_t                  frameId)
		{
			if (hierarchicalTransformComponent->lastUpdatedFrame == frameId) return;

			if (hierarchicalTransformComponent->resolving) {
				EngineAssertMsg(false, "HierarcicalTransformSystem: circular parent detected");
				hierarchicalTransformComponent->DetachParent(self);
				hierarchicalTransformComponent->transform.position = transformComponent->position;
				hierarchicalTransformComponent->transform.scale    = transformComponent->scale;
				hierarchicalTransformComponent->transform.rotation = transformComponent->rotation;
				hierarchicalTransformComponent->lastUpdatedFrame   = frameId;
				return;
			}

			struct ResolvingGuard {
				bool& flag;
				ResolvingGuard(bool& f) : flag(f) { flag = true; }
				~ResolvingGuard()                 { flag = false; }
			} guard(hierarchicalTransformComponent->resolving);

			auto& ctx = EntityContext::Get();

			// 親が無効なら root としてローカルをそのまま使う
			if (!ctx.IsValid(hierarchicalTransformComponent->parentHandle)) {
				hierarchicalTransformComponent->DetachParent(self);
				hierarchicalTransformComponent->transform.position = transformComponent->position;
				hierarchicalTransformComponent->transform.scale    = transformComponent->scale;
				hierarchicalTransformComponent->transform.rotation = transformComponent->rotation;
				hierarchicalTransformComponent->lastUpdatedFrame   = frameId;
				return;
			}

			TransformComponent*             parentTransformComponent             = ctx.GetComponent<TransformComponent>(hierarchicalTransformComponent->parentHandle);
			HierarchicalTransformComponent* parentHierarchicalTransformComponent = ctx.GetComponent<HierarchicalTransformComponent>(hierarchicalTransformComponent->parentHandle);
			if (!parentTransformComponent || !parentHierarchicalTransformComponent) {
				hierarchicalTransformComponent->DetachParent(self);
				hierarchicalTransformComponent->transform.position = transformComponent->position;
				hierarchicalTransformComponent->transform.scale    = transformComponent->scale;
				hierarchicalTransformComponent->transform.rotation = transformComponent->rotation;
				hierarchicalTransformComponent->lastUpdatedFrame   = frameId;
				return;
			}

			// 親を先に解決
			const EntityHandle parentBefore = hierarchicalTransformComponent->parentHandle;
			UpdateWorld(hierarchicalTransformComponent->parentHandle, parentTransformComponent, parentHierarchicalTransformComponent, frameId);

			// 親更新中に循環 detach 等で状態が変化していたら中断
			if (hierarchicalTransformComponent->parentHandle != parentBefore ||
				hierarchicalTransformComponent->lastUpdatedFrame == frameId) return;

			// ワールド座標を計算する (world = local * parent の規約)
			aq::math::Matrix4x4 parentRot;   parentRot.MakeRotationFromQuaternion(parentHierarchicalTransformComponent->transform.rotation);
			aq::math::Matrix4x4 parentScale; parentScale.MakeScaling(parentHierarchicalTransformComponent->transform.scale);
			aq::math::Matrix4x4 parentPos;   parentPos.MakeTranslation(parentHierarchicalTransformComponent->transform.position);

			aq::math::Matrix4x4 parentSR;    parentSR.Mull(parentScale, parentRot);
			aq::math::Matrix4x4 parentWorld; parentWorld.Mull(parentSR, parentPos);

			aq::math::Matrix4x4 localPos;    localPos.MakeTranslation(transformComponent->position);
			aq::math::Matrix4x4 childWorld;  childWorld.Mull(localPos, parentWorld);

			// DirectX 行ベクトル規約: 平行移動は _41/_42/_43 = m[3][0..2]
			hierarchicalTransformComponent->transform.position.x = childWorld.m[3][0];
			hierarchicalTransformComponent->transform.position.y = childWorld.m[3][1];
			hierarchicalTransformComponent->transform.position.z = childWorld.m[3][2];
			hierarchicalTransformComponent->transform.scale      = transformComponent->scale * parentHierarchicalTransformComponent->transform.scale;
			hierarchicalTransformComponent->transform.rotation   = transformComponent->rotation * parentHierarchicalTransformComponent->transform.rotation;
			hierarchicalTransformComponent->lastUpdatedFrame     = frameId;
		}
	}
}
