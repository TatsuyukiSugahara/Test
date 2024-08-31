#include "BulletPhysics.h"
#include "Collider.h"


namespace engine
{
	namespace physics
	{
		RigidBodyInfo::RigidBodyInfo()
			: position(engine::math::Vector3::Zero)
			, rotation(engine::math::Quaternion::Identity)
			, collider(nullptr)
			, mass(0.0f)
		{
		}




		RigidBody::RigidBody()
			: rigidBody_(nullptr)
			, myMotionState_(nullptr)
		{
		}


		RigidBody::~RigidBody()
		{
			Release();
		}


		void RigidBody::Release()
		{
			rigidBody_.release();
			myMotionState_.release();
		}


		void RigidBody::Create(const RigidBodyInfo& info)
		{
			Release();
			btTransform transform;
			transform.setIdentity();
			transform.setOrigin(btVector3(info.position.x, info.position.y, info.position.z));
			transform.setRotation(btQuaternion(info.position.x, info.position.y, info.position.z, info.position.x));
			myMotionState_.reset(new btDefaultMotionState);
			btRigidBody::btRigidBodyConstructionInfo rigidBodyConstructionInfo(info.mass, myMotionState_.get(), info.collider->GetBody(), btVector3(0, 0, 0));
			// „‘Ì¶¬
			rigidBody_.reset(new btRigidBody(rigidBodyConstructionInfo));
		}




		/*******************************************/


		BulletPhysicsWorld::BulletPhysicsWorld()
		{
		}

		BulletPhysicsWorld::~BulletPhysicsWorld()
		{
		}


		void BulletPhysicsWorld::Initialize()
		{

		}


		void BulletPhysicsWorld::Update()
		{

		}


		void BulletPhysicsWorld::AddRigidBody(RigidBody* body)
		{
			dynamicWorld_->addRigidBody(body->GetBody());
		}


		void BulletPhysicsWorld::RemoveRigidBody(RigidBody* body)
		{
			dynamicWorld_->removeRigidBody(body->GetBody());
		}
	}
}