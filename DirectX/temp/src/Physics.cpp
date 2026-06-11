#include "k2EngineLowPreCompile.h"
#include "Physics.h"

using namespace std;
namespace nsK2EngineLow
{
	namespace
	{
		/**
		 * 拡張RayResultCallback
		 * 一番手前のオブジェクトだけを取得するClosestRayResultCallbackを継承
		 */
		struct ExtendedRayResultCallback : public btCollisionWorld::ClosestRayResultCallback
		{
			std::function<bool(const btCollisionObject&)> filterCallback;

			ExtendedRayResultCallback(const btVector3& rayFromWorld, const btVector3& rayToWorld)
				: btCollisionWorld::ClosestRayResultCallback(rayFromWorld, rayToWorld)
			{
			}

			/**
			 * Bulletが「衝突候補」を見つけた時に呼ぶ関数。ここでフィルタリングを行う。
			 */
			virtual bool needsCollision(btBroadphaseProxy* proxy0) const override
			{
				// まず標準のグループ/マスク判定
				bool collides = btCollisionWorld::ClosestRayResultCallback::needsCollision(proxy0);
				if (!collides) return false;

				// ユーザー定義のフィルタがあれば実行
				if (filterCallback) {
					const btCollisionObject* obj = (const btCollisionObject*)proxy0->m_clientObject;
					if (!filterCallback(*obj)) {
						return false; // ユーザー条件で弾く
					}
				}
				return true;
			}
		};


		/**
		 * 拡張ConvexResultCallback
		 * 一番手前のオブジェクトだけを取得するClosestConvexResultCallbackを継承
		 */
		struct ExtendedConvexResultCallback : public btCollisionWorld::ClosestConvexResultCallback
		{
			std::function<bool(const btCollisionObject&)> filterCallback;

			ExtendedConvexResultCallback(const btVector3& convexFromWorld, const btVector3& convexToWorld)
				: btCollisionWorld::ClosestConvexResultCallback(convexFromWorld, convexToWorld)
			{
			}

			virtual bool needsCollision(btBroadphaseProxy* proxy0) const override
			{
				bool collides = btCollisionWorld::ClosestConvexResultCallback::needsCollision(proxy0);
				if (!collides) return false;

				if (filterCallback) {
					const btCollisionObject* obj = (const btCollisionObject*)proxy0->m_clientObject;
					if (!filterCallback(*obj)) {
						return false;
					}
				}
				return true;
			}
		};


		/**
		 * ContactCallback
		 */
		struct MyContactResultCallback : public btCollisionWorld::ContactResultCallback
		{
			using ContantTestCallback = std::function<void(const btCollisionObject& contactCollisionObject)>;
			ContantTestCallback  callback;
			btCollisionObject* me = nullptr;

			btScalar addSingleResult(btManifoldPoint& cp, const btCollisionObjectWrapper* colObj0Wrap, int partId0, int index0, const btCollisionObjectWrapper* colObj1Wrap, int partId1, int index1) override
			{
				if (me == colObj0Wrap->getCollisionObject()) {
					callback(*colObj1Wrap->getCollisionObject());
				}
				return 0.0f;
			}
		};
	}


	/**
	 * PhysicsWorld
	 */


	PhysicsWorld* PhysicsWorld::instance_ = nullptr;


	PhysicsWorld::PhysicsWorld()
	{
		//K2_ASSERT(instance_ == nullptr, "PhysicsWorldのインスタンスを複数作ることはできません。");
		Setup();
	}


	PhysicsWorld::~PhysicsWorld()
	{
		Release();
	}

	void PhysicsWorld::Setup()
	{
		Release();
		collisionConfig_ = std::make_unique<btDefaultCollisionConfiguration>();
		collisionDispatcher_ = std::make_unique<btCollisionDispatcher>(collisionConfig_.get());
		overlappingPairCache_ = std::make_unique<btDbvtBroadphase>();
		constraintSolver_ = std::make_unique<btSequentialImpulseConstraintSolver>();
		dynamicWorld_ = std::make_unique<btDiscreteDynamicsWorld>(
			collisionDispatcher_.get(),
			overlappingPairCache_.get(),
			constraintSolver_.get(),
			collisionConfig_.get()
		);
		dynamicWorld_->setGravity(btVector3(0, -10, 0));

#ifdef K2_DEBUG
		debugWireFrame_.Init();
		dynamicWorld_->setDebugDrawer(&debugWireFrame_);
#endif
	}


	void PhysicsWorld::Release() {
		// 依存関係の逆順で破棄するのが安全
		dynamicWorld_.reset();
		constraintSolver_.reset();
		overlappingPairCache_.reset();
		collisionDispatcher_.reset();
		collisionConfig_.reset();
	}


	void PhysicsWorld::Update(float deltaTime)
	{
		// subSteps=10, fixedTimeStep=1/60.0f など、安定性のための設定を推奨
		dynamicWorld_->stepSimulation(deltaTime, 10);
	}




	void PhysicsWorld::SetGravity(const Vector3& gravity)
	{
		dynamicWorld_->setGravity(ConvertVector3(gravity));
	}


	void PhysicsWorld::AddRigidBody(RigidBody& rb)
	{
		dynamicWorld_->addRigidBody(rb.GetBody());
	}


	void PhysicsWorld::AddRigidBody(RigidBody& rb, void* ptr)
	{
		// UserPointerを設定しておくとRaycastで逆引きできて便利
		rb.GetBody()->setUserPointer(ptr);
		AddRigidBody(rb);
	}


	void PhysicsWorld::RemoveRigidBody(RigidBody& rb)
	{
		dynamicWorld_->removeRigidBody(rb.GetBody());
	}


	void PhysicsWorld::AddCollisionObject(btCollisionObject& colliObj)
	{
		dynamicWorld_->addCollisionObject(&colliObj);
	}


	void PhysicsWorld::RemoveCollisionObject(btCollisionObject& colliObj)
	{
		dynamicWorld_->removeCollisionObject(&colliObj);
	}




	bool PhysicsWorld::Raycast(const Vector3& start, const Vector3& end, const uint32_t filterMask) const
	{
		RaycastHit hit;
		return Raycast(start, end, hit, filterMask, nullptr);
	}


	bool PhysicsWorld::Raycast(const Vector3& rayStart, const Vector3& rayEnd, RaycastHit& result, const uint32_t filterMask, std::function<bool(const btCollisionObject&)> filterCallback) const
	{
		btVector3 start = ConvertVector3(rayStart);
		btVector3 end = ConvertVector3(rayEnd);

		// カスタムコールバックを使用
		ExtendedRayResultCallback cb(start, end);

		// グループとマスクの設定
		// Raycastを行う側(Group)は指定せず CollisionWorld側で「誰と当たるか」をMaskで制御
		cb.m_collisionFilterGroup = -1;
		cb.m_collisionFilterMask = filterMask;
		cb.filterCallback = filterCallback;

		// 実行
		dynamicWorld_->rayTest(start, end, cb);

		if (cb.hasHit()) {
			result.point.Set(cb.m_hitPointWorld.x(), cb.m_hitPointWorld.y(), cb.m_hitPointWorld.z());
			result.normal.Set(cb.m_hitNormalWorld.x(), cb.m_hitNormalWorld.y(), cb.m_hitNormalWorld.z());
			result.distance = start.distance(cb.m_hitPointWorld);
			result.fraction = cb.m_closestHitFraction;
			result.colObject = const_cast<btCollisionObject*>(cb.m_collisionObject);
			result.ptr = result.colObject->getUserPointer();
			return true;
		}
		return false;
	}




	bool PhysicsWorld::ConvexSweepTest(const ICollider& collider, const Vector3& rayStart, const Vector3& rayEnd, SweepHit& result, const uint32_t filterMask, std::function<bool(const btCollisionObject&)> filterCallback) const
	{
		const btConvexShape* convexShape = dynamic_cast<const btConvexShape*>(collider.GetBody());
		if (!convexShape) {
			// Convex以外のShape(Meshなど)ではSweepできません
			return false;
		}
		return ConvexSweepTest(convexShape, rayStart, rayEnd, result, filterMask, filterCallback);
	}


	bool PhysicsWorld::ConvexSweepTest(const btConvexShape* shape, const Vector3& rayStart, const Vector3& rayEnd, SweepHit& result, const uint32_t filterMask, std::function<bool(const btCollisionObject&)> filterCallback) const
	{
		btTransform startTrans, endTrans;
		startTrans.setIdentity();
		endTrans.setIdentity();

		startTrans.setOrigin(ConvertVector3(rayStart));
		endTrans.setOrigin(ConvertVector3(rayEnd));

		// カスタムコールバックを使用
		ExtendedConvexResultCallback cb(startTrans.getOrigin(), endTrans.getOrigin());
		// グループとマスクの設定
		// Raycastを行う側(Group)は指定せず CollisionWorld側で「誰と当たるか」をMaskで制御
		cb.m_collisionFilterGroup = -1;
		cb.m_collisionFilterMask = filterMask;
		cb.filterCallback = filterCallback;
		// 実行
		dynamicWorld_->convexSweepTest(shape, startTrans, endTrans, cb);

		if (cb.hasHit()) {
			result.point.Set(cb.m_hitPointWorld.x(), cb.m_hitPointWorld.y(), cb.m_hitPointWorld.z());
			result.normal.Set(cb.m_hitNormalWorld.x(), cb.m_hitNormalWorld.y(), cb.m_hitNormalWorld.z());
			result.fraction = cb.m_closestHitFraction;
			result.colObject = const_cast<btCollisionObject*>(cb.m_hitCollisionObject);
			result.ptr = result.colObject->getUserPointer();
			return true;
		}
		return false;
	}


	void PhysicsWorld::ConvexSweepTest(const ICollider& collider, const Vector3& start, const Vector3& end, btCollisionWorld::ConvexResultCallback& resultCallback, btScalar allowedCcdPenetration) const
	{
		const btConvexShape* convexShape = dynamic_cast<const btConvexShape*>(collider.GetBody());
		if (!convexShape) {
			// Convex以外のShape(Meshなど)ではSweepできません
			return;
		}
		ConvexSweepTest(convexShape, start, end, resultCallback, allowedCcdPenetration);
	}


	void PhysicsWorld::ConvexSweepTest(const btConvexShape* shape, const Vector3& start, const Vector3& end, btCollisionWorld::ConvexResultCallback& resultCallback, btScalar allowedCcdPenetration) const
	{
		btTransform startTrans, endTrans;
		startTrans.setIdentity();
		endTrans.setIdentity();

		startTrans.setOrigin(ConvertVector3(start));
		endTrans.setOrigin(ConvertVector3(end));

		dynamicWorld_->convexSweepTest(shape, startTrans, endTrans, resultCallback, allowedCcdPenetration);
	}




	void PhysicsWorld::ContactTest(btCollisionObject* colObj, std::function<void(const btCollisionObject& contactCollisionObject)> cb)
	{
		MyContactResultCallback myContactResultCallback;
		myContactResultCallback.callback = cb;
		myContactResultCallback.me = colObj;
		dynamicWorld_->contactTest(colObj, myContactResultCallback);
	}


	void PhysicsWorld::ContactTest(RigidBody& rb, std::function<void(const btCollisionObject&)> cb)
	{
		ContactTest(rb.GetBody(), cb);
	}


	void PhysicsWorld::ContactTest(CharacterController* characterContoller, std::function<void(const btCollisionObject&)> cb)
	{
		// CharacterControllerの実装に依存するが、一般的にRigidBodyを持っている
		ContactTest(*characterContoller->GetRigidBody(), cb);
	}




#ifdef K2_DEBUG
	void PhysicsWorld::DebubDrawWorld(RenderContext& rc) {
		if (isDrawDebugWireFrame_) {
			debugWireFrame_.Begin();
			dynamicWorld_->debugDrawWorld();
			debugWireFrame_.End(rc);
		}
	}
#endif
}