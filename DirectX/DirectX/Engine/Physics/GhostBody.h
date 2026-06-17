#pragma once
#include "GhostPrimitive.h"
#include "PhysicsTypes.h"


namespace aq
{
	namespace physics
	{
		class GhostBodyManager;


		/**
		 * トリガー／センサー用のゴーストコリジョンボディ。
		 * 物理演算（押し戻し）には参加しない。
		 *
		 * バックエンド（Bullet / PhysX）への依存は一切持たない。
		 * ナローフェーズ判定は IGhostNarrowphase 実装クラスが担う。
		 *
		 * 使い方:
		 *   GhostBody body;
		 *   body.CreateSphere(myObj, id, 1.0f, attrMyTeam, maskEnemy);
		 *   body.SetPosition(pos);
		 *   // GhostBodyManager::Get().Update() 後にコールバックで衝突通知を受け取る
		 */
		class GhostBody
		{
			friend class GhostBodyManager;


		public:
			GhostBody();
			~GhostBody();

			GhostBody(const GhostBody&) = delete;
			GhostBody& operator=(const GhostBody&) = delete;


			void CreateSphere (void* owner, uint32_t id, float radius,
			                   uint32_t attr, uint32_t mask);
			void CreateBox    (void* owner, uint32_t id, const math::Vector3& halfExtents,
			                   uint32_t attr, uint32_t mask);
			void CreateCapsule(void* owner, uint32_t id, float radius, float height,
			                   uint32_t attr, uint32_t mask);


			void SetPosition(const math::Vector3& pos);
			void SetRotation(const math::Quaternion& rot);

			const math::Vector3&    GetPosition()  const { return position_; }
			const math::Quaternion& GetRotation()  const { return rotation_; }
			void*    GetOwner()     const { return owner_; }
			uint32_t GetOwnerId()   const { return ownerId_; }
			uint32_t GetAttribute() const { return attribute_; }
			uint32_t GetMask()      const { return mask_; }
			bool     IsActive()     const { return isActive_; }
			void     SetActive(bool active) { isActive_ = active; }

			GhostShapeType  GetShapeType()      const { return shape_ ? shape_->GetType() : GhostShapeType::Sphere; }
			float           GetBoundingRadius() const { return shape_ ? shape_->GetBoundingRadius() : 0.0f; }
			const IGhostShape* GetShape()        const { return shape_.get(); }

			/** Broadphase 内部で使用するハンドル */
			void  SetBroadphaseHandle(void* handle) { broadphaseHandle_ = handle; }
			void* GetBroadphaseHandle() const       { return broadphaseHandle_; }

			/** AABB を計算する（Broadphase が呼ぶ） */
			void ComputeAabb(math::Vector3& aabbMin, math::Vector3& aabbMax) const;

			bool IsDirty()   const { return isDirty_; }
			void ClearDirty()      { isDirty_ = false; }


		private:
			void CreateCore(void* owner, uint32_t id, uint32_t attr, uint32_t mask);

			void*    owner_   = nullptr;
			uint32_t ownerId_ = 0;
			std::unique_ptr<IGhostShape> shape_;

			void* broadphaseHandle_ = nullptr;
			bool  isDirty_  = true;
			bool  isActive_ = true;

			uint32_t attribute_ = kCollisionAttrNone;
			uint32_t mask_      = kCollisionAttrAll;

			math::Vector3    position_;
			math::Quaternion rotation_;
		};
	}
}
