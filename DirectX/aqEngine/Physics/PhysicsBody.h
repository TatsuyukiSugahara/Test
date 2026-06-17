#pragma once
#include "PhysicsBackend.h"


namespace aq
{
	namespace physics
	{
		/**
		 * 物理剛体ラッパー。Box / Sphere / Capsule 形状をワンステップで生成し、
		 * PhysicsWorld への登録も行う。
		 *
		 * mass == 0  → 静的オブジェクト（CF_STATIC_OBJECT）
		 * mass  > 0  → 動的オブジェクト（CF_DYNAMIC_OBJECT）
		 */
		class PhysicsBody
		{
		public:
			PhysicsBody()  = default;
			~PhysicsBody() { Release(); }

			PhysicsBody(const PhysicsBody&) = delete;
			PhysicsBody& operator=(const PhysicsBody&) = delete;


			void CreateBox(const math::Vector3& halfExtents, const math::Vector3& position,
			               uint32_t collisionAttribute,
			               btCollisionObject::CollisionFlags collisionFlags = btCollisionObject::CF_STATIC_OBJECT,
			               float restitution = 0.0f);

			void CreateSphere(float radius, const math::Vector3& position,
			                  uint32_t collisionAttribute,
			                  btCollisionObject::CollisionFlags collisionFlags = btCollisionObject::CF_STATIC_OBJECT,
			                  float restitution = 0.0f);

			void CreateCapsule(float radius, float height, const math::Vector3& position,
			                   uint32_t collisionAttribute,
			                   btCollisionObject::CollisionFlags collisionFlags = btCollisionObject::CF_STATIC_OBJECT,
			                   float restitution = 0.0f);

			/** PhysicsWorld から除去して剛体を破棄 */
			void Release();

			void SetPosition(const math::Vector3& position);
			void SetFriction(float friction) { rigidBody_.SetFriction(friction); }

			btCollisionObject* GetCollisionObject() { return rigidBody_.GetBody(); }


		private:
			void CreateCore(const math::Vector3& position, uint32_t collisionAttribute,
			                btCollisionObject::CollisionFlags collisionFlags, float restitution);

			std::unique_ptr<ICollider> collider_;
			RigidBody                  rigidBody_;
			bool                       addedToWorld_ = false;
		};
	}
}
