#include "stdafx.h"
#include "ActorSteeringComponentSystem.h"
#include "ActorComponentSystem.h"
#include "GameInput.h"

namespace app
{
	namespace ecs
	{
		CharacterSteeringSystem::CharacterSteeringSystem()
		{
		}


		CharacterSteeringSystem::~CharacterSteeringSystem()
		{
		}


		void CharacterSteeringSystem::Update()
		{
			aq::math::Vector3 direction(0.0f);
			float speed = 0.01f;	// 入力速度だが一旦固定
			const auto& input = GameInput::Get();
			if (input.IsPressed(GameAction::MoveForward )) { ++direction.z; }
			if (input.IsPressed(GameAction::MoveBackward)) { --direction.z; }
			if (input.IsPressed(GameAction::MoveLeft    )) { --direction.x; }
			if (input.IsPressed(GameAction::MoveRight   )) { ++direction.x; }


			aq::ecs::Foreach<CharacterSteeringComponent>([direction, speed](const aq::ecs::Entity& entity, CharacterSteeringComponent* component)
				{
					if (!aq::ecs::EntityContext::Get().IsValid(component->GetTarget())) {
						return;
					}

					auto* targetStateMachine = aq::ecs::EntityContext::Get().GetComponent<StateMachineComponent>(component->GetTarget());
					if (targetStateMachine == nullptr) {
						return;
					}

					targetStateMachine->GetStateMachine()->SetDirection(direction);
					targetStateMachine->GetStateMachine()->SetSpeed(speed);
				});
		}
	}
}
