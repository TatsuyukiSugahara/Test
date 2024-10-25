#include "StateMachine.h"
#include "../../Engine/Component/TransformComponentSystem.h"


namespace app
{
	namespace actor
	{
		namespace
		{
			static constexpr uint32_t INVALID_STATE_ID = 0xffffffff;
		}


		/**
		 * ‘Ò‹@
		 */
		IdleState::IdleState(StateMachine* stateMachine)
			: IState(stateMachine)
		{
		}


		IdleState::~IdleState()
		{
		}


		void IdleState::Entry()
		{
		}


		void IdleState::Update()
		{
			// ˆÚ“®
			if (!stateMachine_->GetDirection().IsZero()) {
				stateMachine_->RequestStateID(EngineHash32("Move"));
			}
		}


		void IdleState::Exit()
		{
		}


		/************************************/




		/**
		 * ˆÚ“®
		 */
		MoveState::MoveState(StateMachine* stateMachine)
			: IState(stateMachine)
		{

		}


		MoveState::~MoveState()
		{

		}


		void MoveState::Entry()
		{

		}


		void MoveState::Update()
		{
			engine::math::Vector3 move = stateMachine_->GetDirection();
			if (!move.IsZero()) {
				move.Scale(stateMachine_->GetSpeed());

				const engine::ecs::EntityHandle& targetHandle = stateMachine_->GetTargetHandle();
				if (engine::ecs::EntityManager::Get().IsValid(targetHandle)) {
					auto* transformComponent = engine::ecs::EntityManager::Get().GetComponent<engine::ecs::TransformComponent>(targetHandle);
					transformComponent->transform.localPosition.Add(move);
				}
			} else {
				stateMachine_->RequestStateID(EngineHash32("Idle"));
			}
		}


		void MoveState::Exit()
		{

		}


		/************************************/




		StateMachine::StateMachine()
			: currentState_(nullptr)
			, requestStateId_(INVALID_STATE_ID)
			, targetHandle_()
			, direction_(0.0f)
			, speed_(0.0f)
		{
			stateHashMap_.clear();
		}


		StateMachine::~StateMachine()
		{
			for (auto it : stateHashMap_) {
				delete it.second;
				it.second = nullptr;
			}
			stateHashMap_.clear();
		}


		void StateMachine::Update()
		{
			if (requestStateId_ != INVALID_STATE_ID) {
				if (currentState_) {
					currentState_->Exit();
				}
				currentState_ = stateHashMap_[requestStateId_];
				currentState_->Entry();
				requestStateId_ = INVALID_STATE_ID;
			}

			if (currentState_) {
				currentState_->Update();
			}
		}
	}
}