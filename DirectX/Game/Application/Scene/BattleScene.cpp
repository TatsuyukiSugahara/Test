#include "stdafx.h"
#include "BattleScene.h"

#include "Utility.h"
#include "Component/TerrainComponent.h"  // Utility.h (EngineAssert) より後に include する

#include "Component/TransformComponentSystem.h"
#include "Component/BodyComponentSystem.h"
#include "Component/AnimationComponentSystem.h"
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
			aq::ecs::TerrainComponent* terrainComp = nullptr;
			{
				aq::terrain::HeightmapChunk::Desc desc;
				desc.heightmapPath  = "Assets/Terrain/heightmap.png";
				desc.splatmapPath   = "Assets/Terrain/splatmap.png";
				desc.layerPaths[0]  = "Assets/Terrain/grass.dds";
				desc.layerPaths[1]  = "Assets/Terrain/rock.dds";
				desc.layerPaths[2]  = "Assets/Terrain/dirt.dds";
				desc.resolution     = 128;
				desc.heightScale    = 10.0f;
				desc.terrainSize    = 100.0f;
				desc.layerTiling    = 20.0f;

				auto entity = aq::ecs::EntityContext::Get().CreateEntity<aq::ecs::TransformComponent, aq::ecs::TerrainComponent>();

				auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
				tc->transform.localPosition.Set(-50.0f, 0.0f, -50.0f);
				tc->transform.localAngle.Set(0.0f);
				tc->transform.localScale.Set(1.0f);

				terrainComp = entity.GetComponent<aq::ecs::TerrainComponent>();
				terrainComp->SetDesc(desc);
				terrainComp->GetChunk()->SetReceiveShadow(true);
			}

			// world(0,0) = terrain local(50,50) (XZオフセット -50 適用後)
			const float spawnY = terrainComp->GetChunk()->GetHeight(50.0f, 50.0f);

			// メインカメラ
			aq::Camera* camera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
			camera->SetPosition(aq::math::Vector3(0.0f, spawnY + 5.0f, -15.0f));
			camera->SetTarget(aq::math::Vector3(0.0f, spawnY, 5.0f));
			camera->SetNear(0.01f);
			camera->SetViewportSize(
				static_cast<float>(engine::Engine::Get().GetRenderWidth()),
				static_cast<float>(engine::Engine::Get().GetRenderHeight()));
			camera->Update();

			// オフスクリーンカメラ
			aq::Camera* offscreenCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Offscreen);
			offscreenCamera->SetPosition(aq::math::Vector3(0.0f, spawnY + 5.0f, -15.0f));
			offscreenCamera->SetTarget(aq::math::Vector3(0.0f, spawnY, 5.0f));
			offscreenCamera->SetNear(0.01f);

			// ライト設定
			aq::graphics::LightManager::Get().SetDirectionalColor(aq::math::Vector3(1.0f, 0.6f, 0.6f));

			// スケルタルメッシュキャラクター生成 (Idle アニメーション確認用)
			aq::ecs::EntityHandle targetHandle;

			{
				auto entity = aq::ecs::EntityContext::Get().CreateEntity<
					aq::ecs::TransformComponent,
					aq::ecs::SkeletalMeshComponent,
					aq::ecs::AnimationComponent,
					app::ecs::StateMachineComponent>();

				targetHandle = entity.GetHandle();

				auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
				tc->transform.localPosition.Set(0.0f, spawnY, 0.0f);
				tc->transform.localAngle.Set(0.0f);
				tc->transform.localScale.Set(1.0f);

				auto* skelComp = entity.GetComponent<aq::ecs::SkeletalMeshComponent>();
				skelComp->SetModelPath("Assets/unityChan.tkm");
				skelComp->GetSkeletalMesh()->SetCastShadow(true);
				skelComp->GetSkeletalMesh()->SetReceiveShadow(true);

				auto* animComp = entity.GetComponent<aq::ecs::AnimationComponent>();
				animComp->AddAnimation(aqHash32("idle"), "Assets/animData/idle.tka");
				animComp->Play(aqHash32("idle"), true);

				auto* stateMachineComponent = entity.GetComponent<app::ecs::StateMachineComponent>();
				stateMachineComponent->GetStateMachine()->AddState<app::actor::IdleState>(aqHash32("Idle"));
				stateMachineComponent->GetStateMachine()->AddState<app::actor::MoveState>(aqHash32("Move"));
				stateMachineComponent->GetStateMachine()->RequestStateID(aqHash32("Idle"));
				stateMachineComponent->GetStateMachine()->SetTargetHandle(targetHandle);
			}

			{
				auto entity = aq::ecs::EntityContext::Get().CreateEntity<app::ecs::CharacterSteeringComponent>();
				auto* component = entity.GetComponent<app::ecs::CharacterSteeringComponent>();
				component->SetTarget(targetHandle);
			}
		}


		void BattleScene::Finalize()
		{

		}
	}
}
