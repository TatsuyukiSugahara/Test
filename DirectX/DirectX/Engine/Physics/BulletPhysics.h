#pragma once
#include "btBulletCollisionCommon.h"
#include "btBulletDynamicsCommon.h"
#include "BulletCollision/CollisionDispatch/btGhostObject.h"
#include "Collider.h"
#include "PhysicsTypes.h"
#include "../EnginePreCompile.h"


namespace engine
{
	namespace physics
	{
		/** btVector3 <-> Vector3 変換ユーティリティ */
		inline btVector3          ToBt    (const math::Vector3& v) { return btVector3(v.x, v.y, v.z); }
		inline btQuaternion       ToBtQuat(const math::Quaternion& q) { return btQuaternion(q.x, q.y, q.z, q.w); }
		inline math::Vector3      FromBt  (const btVector3& v)    { return math::Vector3(v.x(), v.y(), v.z()); }
		inline math::Quaternion   FromBtQ (const btQuaternion& q) { return math::Quaternion(q.x(), q.y(), q.z(), q.w()); }


		/*******************************************/


		/**
		 * 剛体生成パラメータ
		 */
		struct RigidBodyInitData
		{
			math::Vector3    position;
			math::Quaternion rotation;
			ICollider*       collider    = nullptr;
			float            mass        = 0.0f;
			float            restitution = 0.0f;
			float            friction    = 0.5f;

			RigidBodyInitData()
				: position(math::Vector3::Zero)
				, rotation(math::Quaternion::Identity)
			{}
		};


		/**
		 * 剛体。BulletPhysics の btRigidBody をラップする。
		 *
		 * mass == 0  → 静的オブジェクト (地形など)
		 * mass  > 0  → 動的オブジェクト (キャラクターなど)
		 */
		class BulletRigidBody
		{
		public:
			BulletRigidBody()  = default;
			~BulletRigidBody() { Release(); }

			BulletRigidBody(const BulletRigidBody&) = delete;
			BulletRigidBody& operator=(const BulletRigidBody&) = delete;

			void Create(const RigidBodyInitData& initData);
			void Release();

			btRigidBody* GetBody() const { return rigidBody_.get(); }


			/** 位置と回転の取得 */
			void GetPositionAndRotation(math::Vector3& pos, math::Quaternion& rot) const;

			/** 位置と回転の設定 */
			void SetPositionAndRotation(const math::Vector3& pos, const math::Quaternion& rot);


			/** 線速度 */
			void          SetLinearVelocity(const math::Vector3& vel);
			math::Vector3 GetLinearVelocity() const;

			/** 角速度 */
			void SetAngularVelocity(const math::Vector3& vel);

			/** 力を加える (relPos は重心からのオフセット) */
			void AddForce(const math::Vector3& force,
			              const math::Vector3& relPos = math::Vector3::Zero);

			/** 瞬間的な衝撃を加える */
			void AddImpulse(const math::Vector3& impulse,
			                const math::Vector3& relPos = math::Vector3::Zero);

			/** 移動可能な軸を制限 (0=ロック, 1=自由) */
			void SetLinearFactor(float x, float y, float z);

			/** 回転可能な軸を制限 (0=ロック, 1=自由) */
			void SetAngularFactor(float x, float y, float z);

			/** 摩擦係数 */
			void SetFriction(float friction);


		private:
			std::unique_ptr<btRigidBody>          rigidBody_;
			std::unique_ptr<btDefaultMotionState> motionState_;
		};


		/*******************************************/


		/**
		 * BulletPhysics を使った物理ワールドのシングルトン。
		 *
		 * ========== 使い方 ==========
		 * // 初期化 (Engine::Initialize の中で呼ぶ)
		 * engine::physics::BulletPhysicsWorld::Initialize();
		 *
		 * // 毎フレーム更新
		 * engine::physics::BulletPhysicsWorld::Get().Update(deltaTime);
		 *
		 * // Raycast
		 * RaycastHit hit;
		 * if (PhysicsWorld::Get().Raycast(from, to, hit)) { ... }
		 *
		 * // バックエンド切り替え → Physics.h の using PhysicsWorld = ... を変えるだけ
		 * ============================
		 */
		class BulletPhysicsWorld
		{
		public:
			~BulletPhysicsWorld() { Finalize(); }

			BulletPhysicsWorld(const BulletPhysicsWorld&) = delete;
			BulletPhysicsWorld& operator=(const BulletPhysicsWorld&) = delete;


			// ===== ライフサイクル =====

			void InitializeWorld();
			void FinalizeWorld();
			void Update(float deltaTime);

			/** 重力設定 (デフォルト: (0, -9.8, 0)) */
			void SetGravity(const math::Vector3& gravity);


			// ===== オブジェクト登録 =====

			/**
			 * 剛体を物理ワールドに追加。
			 * userPtr を設定すると Raycast/ContactTest のコールバック引数から逆引きできる。
			 */
			void AddRigidBody   (BulletRigidBody& rb, void* userPtr = nullptr);
			void RemoveRigidBody(BulletRigidBody& rb);

			void AddCollisionObject   (btCollisionObject& obj, void* userPtr = nullptr);
			void RemoveCollisionObject(btCollisionObject& obj);


			// ===== Raycast =====

			/**
			 * シンプルな Raycast。当たったかどうかだけを返す。
			 * @param filterMask ヒット対象のコリジョン属性マスク (AND 判定)
			 */
			bool Raycast(const math::Vector3& start, const math::Vector3& end,
			             uint32_t filterMask = kCollisionAttrAll) const;

			/**
			 * 詳細 Raycast。ヒット情報を RaycastHit に格納する。
			 * @param filterCallback nullptr 以外を渡すと追加フィルタリングが可能
			 *        (true を返すとヒット対象に含める)
			 */
			bool Raycast(const math::Vector3& start, const math::Vector3& end,
			             RaycastHit& result,
			             uint32_t filterMask = kCollisionAttrAll,
			             std::function<bool(const btCollisionObject&)> filterCallback = nullptr) const;


			// ===== ConvexSweepTest (形状キャスト) =====

			/**
			 * 形状を start → end へ動かした際に最初にヒットするオブジェクトを検索する。
			 * Convex 形状のコライダー (Box/Sphere/Capsule) のみ有効。
			 */
			bool ConvexSweepTest(const ICollider& collider,
			                     const math::Vector3& start, const math::Vector3& end,
			                     SweepHit& result,
			                     uint32_t filterMask = kCollisionAttrAll,
			                     std::function<bool(const btCollisionObject&)> filterCallback = nullptr) const;


			/**
			 * 生の Bullet コールバックを受け取る低レベル ConvexSweep。
			 * CharacterController のような、壁角度フィルタなど
			 * カスタムロジックが必要な場合に使用する。
			 */
			void ConvexSweepTestRaw(const ICollider& collider,
			                        const math::Vector3& start, const math::Vector3& end,
			                        btCollisionWorld::ConvexResultCallback& callback) const;


			// ===== ContactTest (接触判定) =====

			/**
			 * 指定オブジェクトに現在接触しているオブジェクトをコールバックで列挙する。
			 */
			void ContactTest(btCollisionObject& colObj,
			                 std::function<void(const btCollisionObject&)> callback);

			void ContactTest(BulletRigidBody& rb,
			                 std::function<void(const btCollisionObject&)> callback);


			// ===== 上級者向け生アクセス =====

			btDiscreteDynamicsWorld*  GetDynamicWorld()          { return dynamicWorld_.get(); }
			btCollisionDispatcher*    GetCollisionDispatcher()   { return collisionDispatcher_.get(); }
			btDispatcherInfo&         GetDispatchInfo()           { return dynamicWorld_->getDispatchInfo(); }


			// ===== シングルトン =====

			static void                Initialize();
			static void                Finalize();
			static BulletPhysicsWorld& Get();
			static bool                IsInitialized() { return instance_ != nullptr; }

		private:
			BulletPhysicsWorld() = default;

			std::unique_ptr<btDefaultCollisionConfiguration>     collisionConfig_;
			std::unique_ptr<btCollisionDispatcher>               collisionDispatcher_;
			std::unique_ptr<btBroadphaseInterface>               overlappingPairCache_;
			std::unique_ptr<btSequentialImpulseConstraintSolver> constraintSolver_;
			std::unique_ptr<btDiscreteDynamicsWorld>             dynamicWorld_;

			static BulletPhysicsWorld* instance_;
		};
	}
}
