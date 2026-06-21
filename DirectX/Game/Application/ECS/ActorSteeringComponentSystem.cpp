#include "stdafx.h"
#include "ActorSteeringComponentSystem.h"
#include "ActorComponentSystem.h"
#include "GameInput.h"
#include "Graphics/Camera.h"
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
			aq::math::Vector3 inputDir(0.0f);
			const auto& input = GameInput::Get();
			if (input.IsPressed(GameAction::MoveForward )) { ++inputDir.z; }
			if (input.IsPressed(GameAction::MoveBackward)) { --inputDir.z; }
			if (input.IsPressed(GameAction::MoveLeft    )) { --inputDir.x; }
			if (input.IsPressed(GameAction::MoveRight   )) { ++inputDir.x; }

			// カメラ相対のワールド方向に変換（前フレームのカメラ行列を参照）
			aq::math::Vector3 worldDir(0.0f);
			if (!inputDir.IsZero())
			{
				aq::Camera* const cam = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
				if (cam) worldDir = cam->TransformMoveInput(inputDir);
			}

			aq::ecs::Foreach<CharacterSteeringComponent>([this, worldDir](const aq::ecs::Entity& entity, CharacterSteeringComponent* component)
				{
					if (!aq::ecs::EntityContext::Get().IsValid(component->GetTarget())) {
						return;
					}

					auto* targetStateMachine = aq::ecs::EntityContext::Get().GetComponent<StateMachineComponent>(component->GetTarget());
					if (targetStateMachine == nullptr) {
						return;
					}

					targetStateMachine->GetStateMachine()->SetDirection(worldDir);
					targetStateMachine->GetStateMachine()->SetSpeed(speed_);
				});
		}

#ifdef AQ_DEBUG_IMGUI
		void CharacterSteeringSystem::DebugRenderMenu()
		{
			ImGui::MenuItem("Character Steering", nullptr, &show_);
		}

		void CharacterSteeringSystem::DebugRender()
		{
			if (!show_) return;

			if (ImGui::Begin("Character Steering"))
			{
				ImGui::SliderFloat("Speed", &speed_, 0.0f, 0.1f, "%.4f");
			}
			ImGui::End();
		}
#endif
	}
}
