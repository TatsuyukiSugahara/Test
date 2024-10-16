#pragma once
#include "../Utility.h"
#include "../../Engine/ECS/ECS.h"

namespace app
{
	namespace actor
	{
		/**
		 * ��Ԃ̎��
		 */
		struct StateID
		{
			enum
			{
				Idle,
				Move,
			};
		};


		class StateMachine;


		/**
		 * �X�e�[�g�̃x�[�X�N���X
		 */
		class IState
		{
		protected:
			StateMachine* stateMachine_;


		public:
			IState(StateMachine* stateMachine) : stateMachine_(stateMachine) {}

			virtual void Entry() = 0;
			virtual void Update() = 0;
			virtual void Exit() = 0;
		};




		/**
		 * �ҋ@
		 */
		class IdleState : public IState
		{
		public:
			IdleState(StateMachine* stateMachine);
			~IdleState();

			void Entry() override;
			void Update() override;
			void Exit() override;
		};




		/**
		 * �ړ�
		 */
		class MoveState : public IState
		{
		public:
			MoveState(StateMachine* stateMachine);
			~MoveState();

			void Entry() override;
			void Update() override;
			void Exit() override;
		};




		/**
		 * ��Ԃ̑J�ڂ𐧌䂷��N���X
		 */
		class StateMachine
		{
		private:
			/** ��Ԋ֘A */
			IState* currentState_;
			uint32_t requestStateId_;

			/** ��ԑ���Ώ� */
			engine::ecs::EntityHandle targetHandle_;

			/** �ړ��֘A */
			engine::math::Vector3 direction_;
			float speed_;


		public:
			StateMachine();
			~StateMachine();

			void Update();


		public:
			/** ��ԃ��N�G�X�g */
			inline void RequestStateID(const uint32_t request) { requestStateId_ = request; }


		public:
			/** ����Ώېݒ� */
			inline void SetTargetHandle(const engine::ecs::EntityHandle& target) { targetHandle_ = target; }
			/** ����Ώێ擾 */
			inline const engine::ecs::EntityHandle& GetTargetHandle() const { return targetHandle_; }


			/**
			 * �ړ��֘A
			 */
		public:
			/** �ړ����� */
			inline void SetDirection(const engine::math::Vector3& dir)
			{
				direction_.Set(dir);
			}
			inline const engine::math::Vector3& GetDirection() const
			{
				return direction_;
			}

			/** ���x */
			inline void SetSpeed(const float speed)
			{
				speed_ = speed;
			}
			inline float GetSpeed() const
			{
				return speed_;
			}
		};
	}
}