#include "BattleScene.h"

#include "Utility.h"
#include "Terrain/TerrainComponent.h"  // Utility.h (EngineAssert) より後に include する

#include "Component/TransformComponentSystem.h"
#include "Component/BodyComponentSystem.h"
#include "ECS/ActorComponentSystem.h"
#include "ECS/ActorSteeringComponentSystem.h"

#include "Graphics/Camera.h"
#include "Graphics/LightManager.h"
#include "HID/Input.h"


namespace app
{
	namespace battle
	{
		void BattleScene::Update()
		{
		}


		void BattleScene::Initialize()
		{
			// 地形エンティティ生成
			// メッシュは local(0,0,0)~(terrainSize,h,terrainSize) で生成されるため
			// TransformComponent の localPosition を (-half,0,-half) にしてキャラ原点を中心にする
			engine::ecs::TerrainComponent* terrainComp = nullptr;
			{
				engine::terrain::HeightmapChunk::Desc desc;
				desc.heightmapPath = "Assets/Terrain/heightmap.png";
				desc.albedoPath    = "Assets/Terrain/grass.dds";
				desc.resolution    = 128;
				desc.heightScale   = 10.0f;
				desc.terrainSize   = 100.0f;
				desc.uvTiling      = 20.0f;

				auto entity = engine::ecs::EntityContext::Get().CreateEntity<engine::ecs::TransformComponent, engine::ecs::TerrainComponent>();

				auto* tc = entity.GetComponent<engine::ecs::TransformComponent>();
				tc->transform.localPosition.Set(-50.0f, 0.0f, -50.0f);
				tc->transform.localAngle.Set(0.0f);
				tc->transform.localScale.Set(1.0f);

				terrainComp = entity.GetComponent<engine::ecs::TerrainComponent>();
				terrainComp->SetDesc(desc);
				terrainComp->GetChunk()->SetReceiveShadow(true);
			}

			// world(0,0) = terrain local(50,50) (XZオフセット -50 適用後)
			const float spawnY = terrainComp->GetChunk()->GetHeight(50.0f, 50.0f);

			// メインカメラ
			engine::Camera* camera = engine::CameraManager::Get().GetCamera(engine::CameraType::Main);
			camera->SetPosition(engine::math::Vector3(0.0f, spawnY + 5.0f, -15.0f));
			camera->SetTarget(engine::math::Vector3(0.0f, spawnY, 5.0f));
			camera->SetNear(0.01f);
			camera->SetViewportSize(
				static_cast<float>(engine::Engine::Get().GetRenderWidth()),
				static_cast<float>(engine::Engine::Get().GetRenderHeight()));
			camera->Update();

			// オフスクリーンカメラ
			engine::Camera* offscreenCamera = engine::CameraManager::Get().GetCamera(engine::CameraType::Offscreen);
			offscreenCamera->SetPosition(engine::math::Vector3(0.0f, spawnY + 5.0f, -15.0f));
			offscreenCamera->SetTarget(engine::math::Vector3(0.0f, spawnY, 5.0f));
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
				staticMeshComponent->GetStaticMesh()->SetCastShadow(true);
				staticMeshComponent->GetStaticMesh()->SetReceiveShadow(true);

				auto* transformComponent = entity.GetComponent<engine::ecs::TransformComponent>();
				transformComponent->transform.localPosition.Set(0.0f, spawnY, 0.0f);
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