#pragma once

#include <memory>
#include "DebugWireframe.h"

namespace nsK2EngineLow {
	class CharacterController;


	/** btVector3をVector3にコピー */
	inline void Vector3CopyFrom(Vector3& vDst, btVector3& vSrc)
	{
		vDst.x = vSrc.x();
		vDst.y = vSrc.y();
		vDst.z = vSrc.z();
	}

	/** Vector3をbtVector3にコピー */
	inline void Vector3CopyTo(btVector3& vDst, const Vector3& vSrc)
	{
		vDst.setX(vSrc.x);
		vDst.setY(vSrc.y);
		vDst.setZ(vSrc.z);
	}


	/** Vector3をbtVector3に変換 */
	inline btVector3 ConvertVector3(const Vector3& vSrc)
	{
		return btVector3(vSrc.x, vSrc.y, vSrc.z);
	}

	/** btVector3をVector3に変換 */
	inline Vector3 ConvertVector3(const btVector3& vSrc)
	{
		return Vector3(vSrc.x(), vSrc.y(), vSrc.z());
	}


	constexpr uint32_t ALL_COLLISION_ATTRIBUTE_MASK = 0xFFFFFFFF;


	/** レイキャストのヒット情報 */
	struct RaycastHit
	{
		Vector3 point;							// 衝突点
		Vector3 normal;							// 衝突面法線
		float distance;							// 発射点からの距離（0.0f - 1.0f の割合ではなく実距離が必要な場合は別途計算も可能だが、Bulletはfractionを返す）
		float fraction;							// 0.0f(始点) ～ 1.0f(終点) の割合
		btCollisionObject* colObject = nullptr; // ヒットしたBulletオブジェクト
		void* ptr = nullptr;					// ヒットしたオブジェクトのポインタ(UserPointer経由で取得)
	};


	/** スイープテストのヒット情報 */
	struct SweepHit
	{
		Vector3 point;
		Vector3 normal;
		float fraction;
		btCollisionObject* colObject = nullptr;
		void* ptr = nullptr;
	};


	/** 物理空間全体を管理するシングルトンクラス */
	class PhysicsWorld : public Noncopyable
	{
		/** Bulletのコアメンバ */
		std::unique_ptr<btDefaultCollisionConfiguration> collisionConfig_;
		std::unique_ptr<btCollisionDispatcher> collisionDispatcher_;
		std::unique_ptr<btBroadphaseInterface> overlappingPairCache_;
		std::unique_ptr<btSequentialImpulseConstraintSolver> constraintSolver_;
		std::unique_ptr<btDiscreteDynamicsWorld> dynamicWorld_;

#ifdef K2_DEBUG
		DebugWireframe debugWireFrame_;
		bool isDrawDebugWireFrame_ = false;
#endif


	private:
		PhysicsWorld();
		~PhysicsWorld();

		void Setup();

	public:
		void Update(float deltaTime);
		void Release();


		/**
		 * 設定・管理
		 */
		btDiscreteDynamicsWorld* GetDynamicWorld() { return dynamicWorld_.get(); }
		void SetGravity(const Vector3& gravity);
		void AddRigidBody(RigidBody& rb);
		void AddRigidBody(RigidBody& rb, void* ptr);
		void RemoveRigidBody(RigidBody& rb);
		void AddCollisionObject(btCollisionObject& colliObj);
		void RemoveCollisionObject(btCollisionObject& colliObj);




		/**
		 * ========================================================================
		 * Raycast (レイキャスト)
		 * ========================================================================
		 */


		 /**
		  * シンプルなレイキャスト
		  * NOTE: ヒットしたかどうかだけを知りたい場合に使用する
		  */
		bool Raycast(const Vector3& start, const Vector3& end, const uint32_t filterMask = ALL_COLLISION_ATTRIBUTE_MASK) const;

		/**
		 * 詳細なレイキャスト。ヒット情報を受け取る。
		 * @param start 始点
		 * @param end 終点
		 * @param result 結果格納用構造体
		 * @param filterMask 衝突判定を行うレイヤーマスク（ビット演算）
		 * @param filterCallback さらに細かい除外条件を指定するラムダ式（trueを返すとヒット対象）
		 * @return ヒットしたらtrue
		 */
		bool Raycast(const Vector3& start, const Vector3& end, RaycastHit& result, const uint32_t filterMask = ALL_COLLISION_ATTRIBUTE_MASK, std::function<bool(const btCollisionObject&)> filterCallback = nullptr) const;





		/**
		 * ========================================================================
		 * ConvexSweep(形状キャスト)
		 * ========================================================================
		 */


		 /** コライダーを使ったSweepTest */
		bool ConvexSweepTest(const ICollider& collider, const Vector3& start, const Vector3& end, SweepHit& result, const uint32_t filterMask = ALL_COLLISION_ATTRIBUTE_MASK, std::function<bool(const btCollisionObject&)> filterCallback = nullptr) const;
		/**
		 * 生のbtConvexShapeを使ったSweepTest
		 * NOTE: 内部用あるいは上級者用
		 */
		bool ConvexSweepTest(const btConvexShape* shape, const Vector3& start, const Vector3& end, SweepHit& result, const uint32_t filterMask = ALL_COLLISION_ATTRIBUTE_MASK, std::function<bool(const btCollisionObject&)> filterCallback = nullptr) const;

		/** 自由度の高いSweepTest */
		void ConvexSweepTest(const ICollider& collider, const Vector3& start, const Vector3& end, btCollisionWorld::ConvexResultCallback& resultCallback, btScalar allowedCcdPenetration = 0.0f) const;
		void ConvexSweepTest(const btConvexShape* shape, const Vector3& start, const Vector3& end, btCollisionWorld::ConvexResultCallback& resultCallback, btScalar allowedCcdPenetration = 0.0f) const;


		// ========================================================================
		//  ContactTest (接触判定)
		// ========================================================================
		void ContactTest(btCollisionObject* colObj, std::function<void(const btCollisionObject& contactCollisionObject)> cb);
		void ContactTest(RigidBody& rb, std::function<void(const btCollisionObject&)> cb);
		void ContactTest(CharacterController* characterContoller, std::function<void(const btCollisionObject&)> cb);




		/**
		 * デバッグ描画関連
		 */
#ifdef K2_DEBUG 
		void DebubDrawWorld(RenderContext& rc);
		void EnableDrawDebugWireFrame()
		{
			isDrawDebugWireFrame_ = true;
		}
		void DisableDrawDebugWireFrame()
		{
			isDrawDebugWireFrame_ = false;
		}
#endif 


	public:
		/** Bulletのコアメンバ取得用 */
		btCollisionDispatcher* GetCollisionDispatcher() { return collisionDispatcher_.get(); }
		btDispatcherInfo& GetDispatchInfo() { return dynamicWorld_->getDispatchInfo(); }




		/**
		 * シングルト関連
		 */
	private:
		static PhysicsWorld* instance_;


	public:
		static void Initialize()
		{
			if (instance_ == nullptr) {
				instance_ = new PhysicsWorld();
			}
		}
		static PhysicsWorld& Get() { return *instance_; }
		static void Finalize()
		{
			if (instance_) {
				delete instance_;
				instance_ = nullptr;
			}
		}
	};
}
