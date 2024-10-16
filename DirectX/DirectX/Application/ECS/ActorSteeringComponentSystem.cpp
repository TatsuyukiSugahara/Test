#include "ActorSteeringComponentSystem.h""
#include "ActorComponentSystem.h"
#include "../../Engine/HID/Input.h"

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
			engine::math::Vector3 direction(0.0f);
			float speed = 1.0f;	// “ü—Í‘¬“x‚¾‚ªˆê’UŒÅ’è
			if (engine::hid::InputManager::Get().GetKeyBoard().IsPressed(engine::hid::BUTTON_W)) {
				++direction.y;
			}
			if (engine::hid::InputManager::Get().GetKeyBoard().IsPressed(engine::hid::BUTTON_A)) {
				--direction.x;
			}
			if (engine::hid::InputManager::Get().GetKeyBoard().IsPressed(engine::hid::BUTTON_S)) {
				--direction.y;
			}
			if (engine::hid::InputManager::Get().GetKeyBoard().IsPressed(engine::hid::BUTTON_D)) {
				++direction.x;
			}


			engine::ecs::Foreach<CharacterSteeringComponent>([direction, speed](CharacterSteeringComponent* component)
				{
					if (!engine::ecs::EntityManager::Get().IsValid(component->GetTarget())) {
						return;
					}

					auto* targetStateMachine = engine::ecs::EntityManager::Get().GetComponent<StateMachineComponent>(component->GetTarget());
					if (targetStateMachine == nullptr) {
						return;
					}

					targetStateMachine->GetStateMachine()->SetDirection(direction);
					targetStateMachine->GetStateMachine()->SetSpeed(speed);
				});
		}
	}
}