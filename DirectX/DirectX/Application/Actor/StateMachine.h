#pragma once
#include "../Utility.h"
#include "../../Engine/ECS/ECS.h"

namespace app
{
	namespace actor
	{
		/**
		 * 状態の種類
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
		 * ステートのベースクラス
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
		 * 待機
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
		 * 移動
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
		 * 状態の遷移を制御するクラス
		 */
		class StateMachine
		{
		private:
			/** 状態関連 */
			IState* currentState_;
			uint32_t requestStateId_;

			/** 状態操作対象 */
			engine::ecs::EntityHandle targetHandle_;

			/** 移動関連 */
			engine::math::Vector3 direction_;
			float speed_;


		public:
			StateMachine();
			~StateMachine();

			void Update();


		public:
			/** 状態リクエスト */
			inline void RequestStateID(const uint32_t request) { requestStateId_ = request; }


		public:
			/** 操作対象設定 */
			inline void SetTargetHandle(const engine::ecs::EntityHandle& target) { targetHandle_ = target; }
			/** 操作対象取得 */
			inline const engine::ecs::EntityHandle& GetTargetHandle() const { return targetHandle_; }


			/**
			 * 移動関連
			 */
		public:
			/** 移動方向 */
			inline void SetDirection(const engine::math::Vector3& dir)
			{
				direction_.Set(dir);
			}
			inline const engine::math::Vector3& GetDirection() const
			{
				return direction_;
			}

			/** 速度 */
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