#include "BattleScene.h"

#include "../Utility.h"

#include "../../Engine/Component/TransformComponentSystem.h"
#include "../../Engine/Component/BodyComponentSystem.h"
#include "../ECS/ActorComponentSystem.h"
#include "../ECS/ActorSteeringComponentSystem.h"

#include "../../Engine/Graphics/Camera.h"
#include "../../Engine/HID/Input.h"


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
			engine::Camera* camera = engine::CameraManager::Get().GetCamera(engine::CameraType::Main);
			camera->SetPosition(engine::math::Vector3(0.0f, 0.0f, -50.0f));
			camera->SetTarget(engine::math::Vector3(0.0f, 0.0f, 0.0f));
			camera->SetNear(0.01f);
			camera->Update();

			// 操作キャラクター生成
			engine::ecs::EntityHandle targetHandle;
			{
				auto entity = engine::ecs::EntityManager::Get().CreateEntity<engine::ecs::TransformComponent, engine::ecs::BoxStaticMeshComponent, app::ecs::StateMachineComponent>();
				engine::ecs::BoxStaticMeshComponent* boxComponent = engine::ecs::EntityManager::Get().GetComponent<engine::ecs::BoxStaticMeshComponent>(entity);
				boxComponent->Initialize();


				engine::ecs::TransformComponent* transformComponent = engine::ecs::EntityManager::Get().GetComponent<engine::ecs::TransformComponent>(entity);
				transformComponent->transform.localPosition.Set(0.0f);
				transformComponent->transform.localAngle.Set(0.0f);
				transformComponent->transform.localScale.Set(1.0f);

				targetHandle = engine::ecs::EntityManager::Get().GetHandle(entity);
			}
			{
				auto entity = engine::ecs::EntityManager::Get().CreateEntity<app::ecs::CharacterSteeringComponent>();
				auto* component = engine::ecs::EntityManager::Get().GetComponent<app::ecs::CharacterSteeringComponent>(entity);
				component->SetTarget(targetHandle);
			}
		}


		void BattleScene::Finalize()
		{

		}
	}
}