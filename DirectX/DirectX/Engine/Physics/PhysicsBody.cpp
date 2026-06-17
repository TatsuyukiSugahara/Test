#include "aq.h"
#include "PhysicsBody.h"


namespace aq
{
	namespace physics
	{
		void PhysicsBody::CreateBox(
			const math::Vector3& halfExtents,
			const math::Vector3& position,
			uint32_t collisionAttribute,
			btCollisionObject::CollisionFlags collisionFlags,
			float restitution)
		{
			auto* c = new BoxCollider();
			c->Create(halfExtents);
			collider_.reset(c);
			CreateCore(position, collisionAttribute, collisionFlags, restitution);
		}


		void PhysicsBody::CreateSphere(
			float radius,
			const math::Vector3& position,
			uint32_t collisionAttribute,
			btCollisionObject::CollisionFlags collisionFlags,
			float restitution)
		{
			auto* c = new SphereCollider();
			c->Create(radius);
			collider_.reset(c);
			CreateCore(position, collisionAttribute, collisionFlags, restitution);
		}


		void PhysicsBody::CreateCapsule(
			float radius,
			float height,
			const math::Vector3& position,
			uint32_t collisionAttribute,
			btCollisionObject::CollisionFlags collisionFlags,
			float restitution)
		{
			auto* c = new CapsuleCollider();
			c->Create(radius, height);
			collider_.reset(c);
			CreateCore(position, collisionAttribute, collisionFlags, restitution);
		}


		void PhysicsBody::CreateCore(
			const math::Vector3& position,
			uint32_t collisionAttribute,
			btCollisionObject::CollisionFlags collisionFlags,
			float restitution)
		{
			// collider_ は呼び出し元 (CreateBox/Sphere/Capsule) が既に設定済み。
			// ここでは古い剛体だけを除去する (collider_ は触らない)。
			if (addedToWorld_ && rigidBody_.GetBody() && PhysicsWorld::IsInitialized()) {
				PhysicsWorld::Get().RemoveRigidBody(rigidBody_);
			}
			addedToWorld_ = false;
			rigidBody_.Release();

			RigidBodyInitData data;
			data.collider    = collider_.get();
			data.mass        = 0.0f;
			data.restitution = restitution;
			data.position    = position;
			rigidBody_.Create(data);

			rigidBody_.GetBody()->setUserIndex(static_cast<int>(collisionAttribute));
			rigidBody_.GetBody()->setCollisionFlags(collisionFlags);

			PhysicsWorld::Get().AddRigidBody(rigidBody_);
			addedToWorld_ = true;
		}


		void PhysicsBody::Release()
		{
			if (addedToWorld_ && rigidBody_.GetBody() && PhysicsWorld::IsInitialized()) {
				PhysicsWorld::Get().RemoveRigidBody(rigidBody_);
			}
			addedToWorld_ = false;
			rigidBody_.Release();
			collider_.reset();
		}


		void PhysicsBody::SetPosition(const math::Vector3& position)
		{
			if (!rigidBody_.GetBody()) return;
			btTransform& trans = rigidBody_.GetBody()->getWorldTransform();
			trans.setOrigin(ToBt(position));
			rigidBody_.GetBody()->setActivationState(DISABLE_DEACTIVATION);
		}
	}
}
