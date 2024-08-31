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
		 * ���̏��
		 */
		struct RigidBodyInfo
		{
			engine::math::Vector3 position;			// ���W
			engine::math::Quaternion rotation;		// ��]
			ICollider* collider;					// �`��
			float mass;								// ����
			//
			RigidBodyInfo();
		};


		/**
		 * ����
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
		 * Bullet���g�p����PhysicsWorld
		 */
		class BulletPhysicsWorld
		{
		private:
			std::unique_ptr<btDefaultCollisionConfiguration> collisionConfiguration_;
			std::unique_ptr<btCollisionDispatcher> collisionDispatcher_;				// �Փˉ��������B
			std::unique_ptr<btBroadphaseInterface> overlappingPairCache_;				// �u���[�h�t�F�[�Y�B�Փ˔���̎}�؂�B
			std::unique_ptr<btSequentialImpulseConstraintSolver> constraintSolver_;		// �R���X�g���C���g�\���o�[�B�S�������̉��������B
			std::unique_ptr<btDiscreteDynamicsWorld> dynamicWorld_;						// ���[���h�B


		public:
			BulletPhysicsWorld();
			~BulletPhysicsWorld();

			void Initialize();
			void Update();

		public:
			/**
			 * �_�C�i�~�b�N���[���h�擾
			 */
			btDiscreteDynamicsWorld* GetDinamicWorld()
			{
				return dynamicWorld_.get();
			}
			
			/**
			 * ���̂𕨗����[���h�ɒǉ�
			 */
			void AddRigidBody(RigidBody* body);
			/**
			 * ���̂𕨗����[���h����폜
			 */
			void RemoveRigidBody(RigidBody* body);
		};
	}
}