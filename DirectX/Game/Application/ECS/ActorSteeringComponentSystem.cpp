#include "stdafx.h"
#include "ActorSteeringComponentSystem.h"
#include "ActorComponentSystem.h"
#include "GameInput.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#endif

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
			const auto& input = GameInput::Get();
			if (input.IsPressed(GameAction::MoveForward )) { ++direction.z; }
			if (input.IsPressed(GameAction::MoveBackward)) { --direction.z; }
			if (input.IsPressed(GameAction::MoveLeft    )) { --direction.x; }
			if (input.IsPressed(GameAction::MoveRight   )) { ++direction.x; }

			aq::ecs::Foreach<CharacterSteeringComponent>([this, direction](const aq::ecs::Entity& entity, CharacterSteeringComponent* component)
				{
					if (!aq::ecs::EntityContext::Get().IsValid(component->GetTarget())) {
						return;
					}

					auto* targetStateMachine = aq::ecs::EntityContext::Get().GetComponent<StateMachineComponent>(component->GetTarget());
					if (targetStateMachine == nullptr) {
						return;
					}

					targetStateMachine->GetStateMachine()->SetDirection(direction);
					targetStateMachine->GetStateMachine()->SetSpeed(speed_);
				});
		}

#ifdef AQ_DEBUG_IMGUI
		void CharacterSteeringSystem::DebugRender()
		{
			if (ImGui::Begin("Character Steering"))
			{
				ImGui::SliderFloat("Speed", &speed_, 0.0f, 0.1f, "%.4f");
			}
			ImGui::End();
		}
#endif
	}
}
