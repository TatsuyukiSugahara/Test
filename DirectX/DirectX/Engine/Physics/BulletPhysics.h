#pragma once
#include "btBulletCollisionCommon.h"
#include "btBulletDynamicsCommon.h"
#include "BulletCollision/CollisionDispatch/btGhostObject.h"
#include "../EnginePreCompile.h"

namespace engine
{
	namespace physics
	{
		using CollisionShape = btCollisionShape;
		

		class ICollider;




		/*******************************************/


		/**
		 * 剛体情報
		 */
		struct RigidBodyInfo
		{
			engine::math::Vector3 position;			// 座標
			engine::math::Quaternion rotation;		// 回転
			ICollider* collider;					// 形状
			float mass;								// 質量
			//
			RigidBodyInfo();
		};


		/**
		 * 剛体
		 */
		class RigidBody
		{
		private:
			std::unique_ptr<btRigidBody> rigidBody_;
			std::unique_ptr<btDefaultMotionState> myMotionState_;


		public:
			RigidBody();
			~RigidBody();
			void Release();
			void Create(const RigidBodyInfo& info);
			btRigidBody* GetBody() { return rigidBody_.get(); }
		};




		/*******************************************/


		/**
		 * Bulletを使用したPhysicsWorld
		 */
		class BulletPhysicsWorld
		{
		private:
			std::unique_ptr<btDefaultCollisionConfiguration> collisionConfiguration_;
			std::unique_ptr<btCollisionDispatcher> collisionDispatcher_;				// 衝突解決処理。
			std::unique_ptr<btBroadphaseInterface> overlappingPairCache_;				// ブロードフェーズ。衝突判定の枝切り。
			std::unique_ptr<btSequentialImpulseConstraintSolver> constraintSolver_;		// コンストレイントソルバー。拘束条件の解決処理。
			std::unique_ptr<btDiscreteDynamicsWorld> dynamicWorld_;						// ワールド。


		public:
			BulletPhysicsWorld();
			~BulletPhysicsWorld();

			void Initialize();
			void Update();

		public:
			/**
			 * ダイナミックワールド取得
			 */
			btDiscreteDynamicsWorld* GetDinamicWorld()
			{
				return dynamicWorld_.get();
			}
			
			/**
			 * 剛体を物理ワールドに追加
			 */
			void AddRigidBody(RigidBody* body);
			/**
			 * 剛体を物理ワールドから削除
			 */
			void RemoveRigidBody(RigidBody* body);
		};
	}
}