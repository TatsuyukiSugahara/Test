#include "BulletPhysics.h"
#include <cassert>


namespace engine
{
	namespace physics
	{
		namespace
		{
			/**
			 * 一番手前のヒットだけを返す Raycast コールバック。
			 * Bullet の ClosestRayResultCallback を継承し、ユーザー定義フィルタを追加。
			 */
			struct RayResultCallback : public btCollisionWorld::ClosestRayResultCallback
			{
				std::function<bool(const btCollisionObject&)> filterCallback;

				RayResultCallback(const btVector3& from, const btVector3& to)
					: ClosestRayResultCallback(from, to)
				{}

				bool needsCollision(btBroadphaseProxy* proxy) const override
				{
					if (!ClosestRayResultCallback::needsCollision(proxy)) {
						return false;
					}
					if (filterCallback) {
						const auto* obj = static_cast<const btCollisionObject*>(proxy->m_clientObject);
						return filterCallback(*obj);
					}
					return true;
				}
			};


			/**
			 * 一番手前のヒットだけを返す ConvexSweep コールバック。
			 */
			struct ConvexSweepCallback : public btCollisionWorld::ClosestConvexResultCallback
			{
				std::function<bool(const btCollisionObject&)> filterCallback;

				ConvexSweepCallback(const btVector3& from, const btVector3& to)
					: ClosestConvexResultCallback(from, to)
				{}

				bool needsCollision(btBroadphaseProxy* proxy) const override
				{
					if (!ClosestConvexResultCallback::needsCollision(proxy)) {
						return false;
					}
					if (filterCallback) {
						const auto* obj = static_cast<const btCollisionObject*>(proxy->m_clientObject);
						return filterCallback(*obj);
					}
					return true;
				}
			};


			/**
			 * ContactTest コールバック。
			 * 「自分以外に接触しているオブジェクト」をユーザーコールバックへ転送する。
			 */
			struct ContactCallback : public btCollisionWorld::ContactResultCallback
			{
				std::function<void(const btCollisionObject&)> userCallback;
				btCollisionObject* self = nullptr;

				btScalar addSingleResult(
					btManifoldPoint&,
					const btCollisionObjectWrapper* obj0, int, int,
					const btCollisionObjectWrapper* obj1, int, int) override
				{
					if (obj0->getCollisionObject() == self) {
						userCallback(*obj1->getCollisionObject());
					}
					return 0.0f;
				}
			};
		}


		/*******************************************/
		// BulletRigidBody
		/*******************************************/


		void BulletRigidBody::Create(const RigidBodyInitData& initData)
		{
			Release();

			btTransform transform;
			transform.setIdentity();
			transform.setOrigin(ToBt(initData.position));
			transform.setRotation(ToBtQuat(initData.rotation));

			motionState_.reset(new btDefaultMotionState(transform));

			btVector3 localInertia(0, 0, 0);
			if (initData.mass > 0.0f && initData.collider) {
				initData.collider->GetShape()->calculateLocalInertia(initData.mass, localInertia);
			}

			btCollisionShape* shape = initData.collider ? initData.collider->GetShape() : nullptr;
			btRigidBody::btRigidBodyConstructionInfo rbInfo(
				initData.mass, motionState_.get(), shape, localInertia);
			rbInfo.m_restitution = initData.restitution;
			rbInfo.m_friction    = initData.friction;

			rigidBody_.reset(new btRigidBody(rbInfo));
		}


		void BulletRigidBody::Release()
		{
			rigidBody_.reset();
			motionState_.reset();
		}


		void BulletRigidBody::GetPositionAndRotation(math::Vector3& pos, math::Quaternion& rot) const
		{
			btTransform trans;
			motionState_->getWorldTransform(trans);
			pos = FromBt(trans.getOrigin());
			rot = FromBtQ(trans.getRotation());
		}


		void BulletRigidBody::SetPositionAndRotation(const math::Vector3& pos, const math::Quaternion& rot)
		{
			btTransform trans;
			trans.setOrigin(ToBt(pos));
			trans.setRotation(ToBtQuat(rot));
			rigidBody_->setWorldTransform(trans);
			motionState_->setWorldTransform(trans);
			rigidBody_->activate();
		}


		void BulletRigidBody::SetLinearVelocity(const math::Vector3& vel)
		{
			rigidBody_->setLinearVelocity(ToBt(vel));
			rigidBody_->activate();
		}


		math::Vector3 BulletRigidBody::GetLinearVelocity() const
		{
			return FromBt(rigidBody_->getLinearVelocity());
		}


		void BulletRigidBody::SetAngularVelocity(const math::Vector3& vel)
		{
			rigidBody_->setAngularVelocity(ToBt(vel));
			rigidBody_->activate();
		}


		void BulletRigidBody::AddForce(const math::Vector3& force, const math::Vector3& relPos)
		{
			rigidBody_->applyForce(ToBt(force), ToBt(relPos));
			rigidBody_->activate();
		}


		void BulletRigidBody::AddImpulse(const math::Vector3& impulse, const math::Vector3& relPos)
		{
			rigidBody_->applyImpulse(ToBt(impulse), ToBt(relPos));
			rigidBody_->activate();
		}


		void BulletRigidBody::SetLinearFactor(float x, float y, float z)
		{
			rigidBody_->setLinearFactor(btVector3(x, y, z));
		}


		void BulletRigidBody::SetAngularFactor(float x, float y, float z)
		{
			rigidBody_->setAngularFactor(btVector3(x, y, z));
		}


		void BulletRigidBody::SetFriction(float friction)
		{
			rigidBody_->setFriction(friction);
			rigidBody_->setRollingFriction(friction);
		}


		/*******************************************/
		// BulletPhysicsWorld - シングルトン
		/*******************************************/


		BulletPhysicsWorld* BulletPhysicsWorld::instance_ = nullptr;


		void BulletPhysicsWorld::Initialize()
		{
			assert(!instance_ && "BulletPhysicsWorld::Initialize called twice");
			instance_ = new BulletPhysicsWorld();
			instance_->InitializeWorld();
		}


		void BulletPhysicsWorld::Finalize()
		{
			if (instance_) {
				instance_->FinalizeWorld();
				delete instance_;
				instance_ = nullptr;
			}
		}


		BulletPhysicsWorld& BulletPhysicsWorld::Get()
		{
			assert(instance_ && "BulletPhysicsWorld not initialized");
			return *instance_;
		}


		/*******************************************/
		// BulletPhysicsWorld - 初期化・更新
		/*******************************************/


		void BulletPhysicsWorld::InitializeWorld()
		{
			collisionConfig_     = std::make_unique<btDefaultCollisionConfiguration>();
			collisionDispatcher_ = std::make_unique<btCollisionDispatcher>(collisionConfig_.get());
			overlappingPairCache_= std::make_unique<btDbvtBroadphase>();
			constraintSolver_    = std::make_unique<btSequentialImpulseConstraintSolver>();
			dynamicWorld_        = std::make_unique<btDiscreteDynamicsWorld>(
				collisionDispatcher_.get(),
				overlappingPairCache_.get(),
				constraintSolver_.get(),
				collisionConfig_.get()
			);
			dynamicWorld_->setGravity(btVector3(0.0f, -9.8f, 0.0f));
		}


		void BulletPhysicsWorld::FinalizeWorld()
		{
			// 依存関係の逆順で破棄
			dynamicWorld_.reset();
			constraintSolver_.reset();
			overlappingPairCache_.reset();
			collisionDispatcher_.reset();
			collisionConfig_.reset();
		}


		void BulletPhysicsWorld::Update(float deltaTime)
		{
			// subSteps=10 で安定したシミュレーションを行う
			dynamicWorld_->stepSimulation(deltaTime, 10);
		}


		void BulletPhysicsWorld::SetGravity(const math::Vector3& gravity)
		{
			dynamicWorld_->setGravity(ToBt(gravity));
		}


		/*******************************************/
		// BulletPhysicsWorld - オブジェクト管理
		/*******************************************/


		void BulletPhysicsWorld::AddRigidBody(BulletRigidBody& rb, void* userPtr)
		{
			if (userPtr) {
				rb.GetBody()->setUserPointer(userPtr);
			}
			dynamicWorld_->addRigidBody(rb.GetBody());
		}


		void BulletPhysicsWorld::RemoveRigidBody(BulletRigidBody& rb)
		{
			dynamicWorld_->removeRigidBody(rb.GetBody());
		}


		void BulletPhysicsWorld::AddCollisionObject(btCollisionObject& obj, void* userPtr)
		{
			if (userPtr) {
				obj.setUserPointer(userPtr);
			}
			dynamicWorld_->addCollisionObject(&obj);
		}


		void BulletPhysicsWorld::RemoveCollisionObject(btCollisionObject& obj)
		{
			dynamicWorld_->removeCollisionObject(&obj);
		}


		/*******************************************/
		// BulletPhysicsWorld - Raycast
		/*******************************************/


		bool BulletPhysicsWorld::Raycast(
			const math::Vector3& start,
			const math::Vector3& end,
			uint32_t filterMask) const
		{
			RaycastHit dummy;
			return Raycast(start, end, dummy, filterMask, nullptr);
		}


		bool BulletPhysicsWorld::Raycast(
			const math::Vector3& start,
			const math::Vector3& end,
			RaycastHit& result,
			uint32_t filterMask,
			std::function<bool(const btCollisionObject&)> filterCallback) const
		{
			btVector3 btStart = ToBt(start);
			btVector3 btEnd   = ToBt(end);

			RayResultCallback cb(btStart, btEnd);
			cb.m_collisionFilterGroup = -1;
			cb.m_collisionFilterMask  = static_cast<int>(filterMask);
			cb.filterCallback         = std::move(filterCallback);

			dynamicWorld_->rayTest(btStart, btEnd, cb);

			if (!cb.hasHit()) {
				return false;
			}

			result.point    = FromBt(cb.m_hitPointWorld);
			result.normal   = FromBt(cb.m_hitNormalWorld);
			result.distance = btStart.distance(cb.m_hitPointWorld);
			result.fraction = cb.m_closestHitFraction;
			result.userPtr  = const_cast<btCollisionObject*>(cb.m_collisionObject)->getUserPointer();
			return true;
		}


		/*******************************************/
		// BulletPhysicsWorld - ConvexSweepTest
		/*******************************************/


		bool BulletPhysicsWorld::ConvexSweepTest(
			const ICollider& collider,
			const math::Vector3& start,
			const math::Vector3& end,
			SweepHit& result,
			uint32_t filterMask,
			std::function<bool(const btCollisionObject&)> filterCallback) const
		{
			const auto* convexShape = dynamic_cast<const btConvexShape*>(collider.GetShape());
			if (!convexShape) {
				// Mesh コライダー等 Convex でない形状はスイープ不可
				return false;
			}

			btVector3 btStart = ToBt(start);
			btVector3 btEnd   = ToBt(end);

			btTransform startTrans, endTrans;
			startTrans.setIdentity();
			endTrans.setIdentity();
			startTrans.setOrigin(btStart);
			endTrans.setOrigin(btEnd);

			ConvexSweepCallback cb(btStart, btEnd);
			cb.m_collisionFilterGroup = -1;
			cb.m_collisionFilterMask  = static_cast<int>(filterMask);
			cb.filterCallback         = std::move(filterCallback);

			dynamicWorld_->convexSweepTest(convexShape, startTrans, endTrans, cb);

			if (!cb.hasHit()) {
				return false;
			}

			result.point    = FromBt(cb.m_hitPointWorld);
			result.normal   = FromBt(cb.m_hitNormalWorld);
			result.fraction = cb.m_closestHitFraction;
			result.userPtr  = const_cast<btCollisionObject*>(cb.m_hitCollisionObject)->getUserPointer();
			return true;
		}


		/*******************************************/
		// BulletPhysicsWorld - ConvexSweepTestRaw
		/*******************************************/


		void BulletPhysicsWorld::ConvexSweepTestRaw(
			const ICollider& collider,
			const math::Vector3& start,
			const math::Vector3& end,
			btCollisionWorld::ConvexResultCallback& callback) const
		{
			const auto* convexShape = dynamic_cast<const btConvexShape*>(collider.GetShape());
			if (!convexShape) return;

			btTransform startTrans, endTrans;
			startTrans.setIdentity();
			endTrans.setIdentity();
			startTrans.setOrigin(ToBt(start));
			endTrans.setOrigin(ToBt(end));

			dynamicWorld_->convexSweepTest(convexShape, startTrans, endTrans, callback);
		}


		/*******************************************/
		// BulletPhysicsWorld - ContactTest
		/*******************************************/


		void BulletPhysicsWorld::ContactTest(
			btCollisionObject& colObj,
			std::function<void(const btCollisionObject&)> callback)
		{
			ContactCallback cb;
			cb.self         = &colObj;
			cb.userCallback = std::move(callback);
			dynamicWorld_->contactTest(&colObj, cb);
		}


		void BulletPhysicsWorld::ContactTest(
			BulletRigidBody& rb,
			std::function<void(const btCollisionObject&)> callback)
		{
			ContactTest(*rb.GetBody(), std::move(callback));
		}
	}
}
