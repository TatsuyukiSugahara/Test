#include "BattleScene.h"

#include "../Utility.h"

#include "Engine/Component/TransformComponentSystem.h"
#include "Engine/Component/BodyComponentSystem.h"
#include "../ECS/ActorComponentSystem.h"
#include "../ECS/ActorSteeringComponentSystem.h"

#include "Engine/Graphics/Camera.h"
#include "Engine/Graphics/LightManager.h"
#include "Engine/HID/Input.h"


namespace app
{
	namespace battle
	{
		BattleScene::BattleScene()
		{
		}


		BattleScene::~BattleScene()
		{
		}


		void BattleScene::Update()
		{

		}


		void BattleScene::Initialize()
		{
			// メインカメラ
			engine::Camera* camera = engine::CameraManager::Get().GetCamera(engine::CameraType::Main);
			camera->SetPosition(engine::math::Vector3(0.0f, 0.0f, -10.0f));
			camera->SetTarget(engine::math::Vector3(0.0f, 0.0f, 0.0f));
			camera->SetNear(0.01f);
			camera->SetViewportSize(
				static_cast<float>(engine::Engine::Get().GetRenderWidth()),
				static_cast<float>(engine::Engine::Get().GetRenderHeight()));
			camera->Update();

			// オフスクリーンカメラ
			// アスペクト比は Application::Initialize() でオフスクリーン RT サイズから設定済み。
			// TODO: シーンに応じた別アングルを設定する（現在はメインと同一位置）。
			engine::Camera* offscreenCamera = engine::CameraManager::Get().GetCamera(engine::CameraType::Offscreen);
			offscreenCamera->SetPosition(engine::math::Vector3(0.0f, 0.0f, -10.0f));
			offscreenCamera->SetTarget(engine::math::Vector3(0.0f, 0.0f, 0.0f));
			offscreenCamera->SetNear(0.01f);

			// ライト設定
			engine::graphics::LightManager::Get().SetDirectionalColor(engine::math::Vector3(1.0f, 0.6f, 0.6f));

			// 操作キャラクター生成
			engine::ecs::EntityHandle targetHandle;
			{
				auto entity = engine::ecs::EntityContext::Get().CreateEntity<engine::ecs::TransformComponent, engine::ecs::StaticMeshComponent, app::ecs::StateMachineComponent>();

				targetHandle = entity.GetHandle();

				auto* stateMachineComponent = entity.GetComponent<app::ecs::StateMachineComponent>();
				stateMachineComponent->GetStateMachine()->AddState<app::actor::IdleState>(EngineHash32("Idle"));
				stateMachineComponent->GetStateMachine()->AddState<app::actor::MoveState>(EngineHash32("Move"));
				stateMachineComponent->GetStateMachine()->RequestStateID(EngineHash32("Idle"));
				stateMachineComponent->GetStateMachine()->SetTargetHandle(targetHandle);

				auto* staticMeshComponent = entity.GetComponent<engine::ecs::StaticMeshComponent>();
				staticMeshComponent->SetModelPath("../temp/Assets/unityChan.tkm");

				auto* transformComponent = entity.GetComponent<engine::ecs::TransformComponent>();
				transformComponent->transform.localPosition.Set(0.0f);
				transformComponent->transform.localAngle.Set(0.0f);
				transformComponent->transform.localScale.Set(1.0f);
			}
			{
				auto entity = engine::ecs::EntityContext::Get().CreateEntity<app::ecs::CharacterSteeringComponent>();
				auto* component = entity.GetComponent<app::ecs::CharacterSteeringComponent>();
				component->SetTarget(targetHandle);
			}
		}


		void BattleScene::Finalize()
		{

		}
	}
}