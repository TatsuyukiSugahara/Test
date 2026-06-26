#include "stdafx.h"
#include "BattleScene.h"

#include "Component/TerrainComponent.h"
#include "Component/OceanComponent.h"
#ifdef AQ_DEBUG_IMGUI
#include "Core/DebugUI.h"
#include "ECS/EntityDebugTag.h"
#endif

#include "Component/Prefab.h"
#include "Component/AnimationComponentSystem.h"
#include "ECS/ActorComponentSystem.h"
#include "ECS/ActorSteeringComponentSystem.h"
#include "ECS/CameraSteeringComponentSystem.h"


namespace app
{
	namespace battle
	{
		void BattleScene::Update()
		{
		}


		aq::math::Vector3 BattleScene::GetFocusPosition() const
		{
			if (!playerHandle_.IsValid()) return aq::math::Vector3(0.f, 0.f, 0.f);
			auto* hierarchicalTransformComponent = aq::ecs::EntityContext::Get().GetComponent<aq::ecs::HierarchicalTransformComponent>(playerHandle_);
			if (!hierarchicalTransformComponent) return aq::math::Vector3(0.f, 0.f, 0.f);
			return hierarchicalTransformComponent->transform.position;
		}


		void BattleScene::Initialize()
		{
			// 地形エンティティ生成
			// メッシュは local(0,0,0)~(terrainSize,h,terrainSize) で生成されるため
			// TransformComponent の localPosition を (-half,0,-half) にしてキャラ原点を中心にする
			aq::ecs::TerrainComponent*   terrainComp = nullptr;
			aq::ecs::TransformComponent* terrainTC   = nullptr;
			{
				aq::terrain::HeightmapChunk::Desc desc;
				desc.heightmapPath  = "Assets/Terrain/heightmap.png";
				desc.splatmapPath   = "Assets/Terrain/splatmap.png";
				desc.layerPaths[0]  = "Assets/Terrain/grass.DDS";
				desc.layerPaths[1]  = "Assets/Terrain/snow.DDS";
				desc.layerPaths[2]  = "Assets/Terrain/rock.DDS";
				desc.resolution     = 128;
				desc.heightScale    = 10.0f;
				desc.terrainSize    = 100.0f;
				desc.layerTiling    = 20.0f;

				auto entity = aq::ecs::EntityContext::Get().CreateEntity<aq::ecs::TransformComponent, aq::ecs::HierarchicalTransformComponent, aq::ecs::TerrainComponent>();

				auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
				terrainTC = tc;
				tc->position.Set(-50.0f, 0.0f, -50.0f);
				tc->scale.Set(1.0f);

				terrainComp = entity.GetComponent<aq::ecs::TerrainComponent>();
				terrainComp->SetDesc(desc);
				terrainComp->GetChunk()->SetReceiveShadow(true);
#ifdef AQ_DEBUG_IMGUI
				entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Terrain");
#endif
			}

#ifdef AQ_DEBUG_IMGUI
			painter_.Attach(terrainComp->GetChunk(), aq::math::Vector3(-50.0f, 0.0f, -50.0f),
			                terrainTC ? &terrainTC->position : nullptr);
			splatmapPainter_.Attach(terrainComp->GetChunk(), aq::math::Vector3(-50.0f, 0.0f, -50.0f));
			aq::DebugUI::Get().Register(&painter_);
			aq::DebugUI::Get().Register(&splatmapPainter_);
#endif

			// world(0,0) = terrain local(50,50) (XZオフセット -50 適用後)
			const float spawnY = terrainComp->GetChunk()->GetHeight(50.0f, 50.0f);

			// 海エンティティ (地形の下に配置して水面を演出)
			{
				auto entity = aq::ecs::EntityContext::Get().CreateEntity<
					aq::ecs::TransformComponent, aq::ecs::HierarchicalTransformComponent, aq::ecs::OceanComponent>();

				auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
				tc->position.Set(-100.0f, -0.5f, -100.0f);
				tc->scale.Set(1.0f);

				aq::ocean::OceanParams params;
				params.size       = 300.0f;  // 地形 (100m) より広い範囲をカバー
				params.resolution = 128;

				// 海の色
				params.deepColor    = { 0.02f, 0.06f, 0.14f };
				params.shallowColor = { 0.07f, 0.22f, 0.38f };

				// Fresnel
				params.fresnelBias  = 0.04f;
				params.fresnelScale = 1.0f;
				params.fresnelPower = 4.0f;

				// 太陽ハイライト
				params.sunShininess = 256.0f;
				params.sunIntensity = 2.5f;
				params.skyColor     = { 0.55f, 0.75f, 0.95f };

				// Gerstner 波パラメータ (デフォルト値を使用)
				// params.waves[0..3] はデフォルトで主うねり・斜め・斜め逆・チョップが設定済み

				entity.GetComponent<aq::ecs::OceanComponent>()->Initialize(params);
#ifdef AQ_DEBUG_IMGUI
				entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Ocean");
#endif
			}

			// メインカメラの Near・アスペクト比設定（位置/注視点は CameraSteeringSystem が管理）
			aq::Camera* const mainCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
			mainCamera->SetNear(0.01f);
			mainCamera->SetViewportSize(
				static_cast<float>(aq::Engine::Get().GetRenderWidth()),
				static_cast<float>(aq::Engine::Get().GetRenderHeight()));

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
					aq::ecs::HierarchicalTransformComponent,
					aq::ecs::SkeletalMeshComponent,
					aq::ecs::AnimationComponent,
					app::ecs::StateMachineComponent>();

				targetHandle = entity.GetHandle();
				playerHandle_ = targetHandle;

				auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
				tc->position.Set(0.0f, spawnY, 0.0f);
				tc->scale.Set(1.0f);

				auto* skelComp = entity.GetComponent<aq::ecs::SkeletalMeshComponent>();
				// ディファード PBR で描画（SetTranslucent によるディザ半透明を使うため）。
				// SetShaderType は SetModelPath より前に呼ぶこと（ロード後の変更は再初期化されない）。
				skelComp->SetShaderType(aq::graphics::SkeletalMesh::ShaderType::SkeletalPBRLit);
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
#ifdef AQ_DEBUG_IMGUI
				entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Player");
#endif
			}

			{
				auto entity = aq::ecs::EntityContext::Get().CreateEntity<app::ecs::CharacterSteeringComponent>();
				auto* const component = entity.GetComponent<app::ecs::CharacterSteeringComponent>();
				component->SetTarget(targetHandle);
#ifdef AQ_DEBUG_IMGUI
				entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("CharacterSteering");
#endif
			}

			// カメラエンティティ（プレイヤー追従 ManualView TPS）
			// Note: Initialize() は ECS::Update() より前に呼ばれるため、
			//       シーン遷移がフレーム途中の場合は初回 1 フレームのみデフォルト姿勢になる
			{
				auto entity = aq::ecs::EntityContext::Get().CreateEntity<app::ecs::CameraSteeringComponent>();
				auto* const cam = entity.GetComponent<app::ecs::CameraSteeringComponent>();
				cam->TrackEntity(targetHandle, aq::math::Vector3(0.0f, 1.5f, 0.0f));
				cam->SetManualView(0.0f, 20.0f, 10.0f);
				cam->cameraType = aq::CameraType::Main;
#ifdef AQ_DEBUG_IMGUI
				entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("CameraTracking");
#endif
			}

			// カメラエフェクトエンティティ（シェイク等の演出加算。TriggerShake() で起動）
			{
				auto entity = aq::ecs::EntityContext::Get().CreateEntity<app::ecs::CameraEffectComponent>();
				auto* const effect = entity.GetComponent<app::ecs::CameraEffectComponent>();
				effect->cameraType = aq::CameraType::Main;
#ifdef AQ_DEBUG_IMGUI
				entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("CameraEffect");
#endif
			}

			// ----- Prefab 階層テスト -----
			// ローカル座標と期待ワールド座標:
			//   root        local (0, spawnY, 5)   → world (0, spawnY, 5)
			//   └ child     local (+3, 0, 0)        → world (3, spawnY, 5)
			//     └ grandchild local (0, +2, 0)     → world (3, spawnY+2, 5)
			// HierarcicalTransformSystem が 1 フレーム後に HTC.transform を更新する。
			{
				auto grandchildPrefab = aq::ecs::Prefab::Create<
					aq::ecs::TransformComponent,
					aq::ecs::HierarchicalTransformComponent,
					aq::ecs::BoxStaticMeshComponent>(
					"PrefabTest_Grandchild",
					[](aq::ecs::Entity entity)
					{
						entity.GetComponent<aq::ecs::TransformComponent>()->position.Set(0.0f, 2.0f, 0.0f);
					});

				auto childPrefab = aq::ecs::Prefab::Create<
					aq::ecs::TransformComponent,
					aq::ecs::HierarchicalTransformComponent,
					aq::ecs::BoxStaticMeshComponent>(
					"PrefabTest_Child",
					[](aq::ecs::Entity entity)
					{
						entity.GetComponent<aq::ecs::TransformComponent>()->position.Set(3.0f, 0.0f, 0.0f);
					});
				childPrefab.AddChild(grandchildPrefab);

				auto rootPrefab = aq::ecs::Prefab::Create<
					aq::ecs::TransformComponent,
					aq::ecs::HierarchicalTransformComponent,
					aq::ecs::BoxStaticMeshComponent>(
					"PrefabTest_Root",
					[spawnY](aq::ecs::Entity entity)
					{
						entity.GetComponent<aq::ecs::TransformComponent>()->position.Set(0.0f, spawnY, 5.0f);
					});
				rootPrefab.AddChild(childPrefab);

				rootPrefab.Instantiate();
			}
		}


		void BattleScene::Finalize()
		{
#ifdef AQ_DEBUG_IMGUI
			aq::DebugUI::Get().Unregister(&splatmapPainter_);
			aq::DebugUI::Get().Unregister(&painter_);
			splatmapPainter_.Detach();
			painter_.Detach();
#endif
		}
	}
}
