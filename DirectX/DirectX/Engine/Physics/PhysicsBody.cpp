#include "PhysicsBody.h"


namespace engine
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
			auto* c = new BulletBoxCollider();
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
			auto* c = new BulletSphereCollider();
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
			auto* c = new BulletCapsuleCollider();
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
			Release();

			RigidBodyInitData data;
			data.collider    = collider_.get();
			data.mass        = 0.0f;
			data.restitution = restitution;
			data.position    = position;
			rigidBody_.Create(data);

			rigidBody_.GetBody()->setUserIndex(static_cast<int>(collisionAttribute));
			rigidBody_.GetBody()->setCollisionFlags(collisionFlags);

			BulletPhysicsWorld::Get().AddRigidBody(rigidBody_);
			addedToWorld_ = true;
		}


		void PhysicsBody::Release()
		{
			if (addedToWorld_ && rigidBody_.GetBody() && BulletPhysicsWorld::IsInitialized()) {
				BulletPhysicsWorld::Get().RemoveRigidBody(rigidBody_);
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
