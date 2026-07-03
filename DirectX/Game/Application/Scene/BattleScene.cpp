#include "stdafx.h"
#include "BattleScene.h"

#include "Component/TerrainComponent.h"
#include "Component/OceanComponent.h"
#include "Component/DecalComponent.h"
#ifdef AQ_DEBUG_IMGUI
#include "Core/DebugUI.h"
#include "ECS/EntityDebugTag.h"
#endif

#include "ECS/Prefab.h"
#include "ECS/PrefabSerializer.h"
#include "ECS/PrefabRegistry.h"
#include "ECS/SpawnSystem.h"
#include "ECS/JsonFieldVisitor.h"
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
				skelComp->GetSkeletalMesh()->SetReceivesDecal(false);  // プレイヤーはデカール非対象

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

			// FBX 読み込みデモ: Character.fbx をスケルタルメッシュ+テクスチャで読み込み、
			// 同一 FBX 内の複数アニメクリップを名前指定("path.fbx#ClipName")で登録して再生する。
			// このモデルは 5 クリップ(Happy/Idle/Sad/Startled/ThrowFish)を持つ。
			// スケールは FBX が約 0.87m と小さく、この海シーンの単位規約に合わせ拡大する。
			{
				auto entity = aq::ecs::EntityContext::Get().CreateEntity<
					aq::ecs::TransformComponent,
					aq::ecs::HierarchicalTransformComponent,
					aq::ecs::SkeletalMeshComponent,
					aq::ecs::AnimationComponent>();

				auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
				tc->position.Set(0.0f, spawnY + 10.0f, 6.0f);
				tc->scale.Set(60.0f);

				auto* skelComp = entity.GetComponent<aq::ecs::SkeletalMeshComponent>();
				skelComp->SetShaderType(aq::graphics::SkeletalMesh::ShaderType::SkeletalModelLit);
				skelComp->SetModelPath("Assets/Character/Character.fbx"); // アルベドは FBX 参照テクスチャを自動解決
				// 向きは FBX ローダーの座標系設定で立たせている (エンティティ回転はゲーム操作用に空ける)。
				skelComp->GetSkeletalMesh()->SetCastShadow(true);
				skelComp->GetSkeletalMesh()->SetReceiveShadow(true);

				auto* animComp = entity.GetComponent<aq::ecs::AnimationComponent>();
				// 同一 FBX から複数クリップを登録 (名前指定)。インデックス指定 "#4" も可。
				animComp->AddAnimation(aqHash32("Idle"),      "Assets/Character/Character.fbx#Idle");
				animComp->AddAnimation(aqHash32("Happy"),     "Assets/Character/Character.fbx#Happy");
				animComp->AddAnimation(aqHash32("ThrowFish"), "Assets/Character/Character.fbx#ThrowFish");
				animComp->Play(aqHash32("Happy"), true);

#ifdef AQ_DEBUG_IMGUI
				entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("FBX_Character");
#endif
			}

			// 確認用デカール: プレイヤー足元の地面に真下投影で 1 枚配置。
			// 縦に長い箱 (y=深度) にして地形と確実に交差させる。
			{
				auto entity = aq::ecs::EntityContext::Get().CreateEntity<
					aq::ecs::TransformComponent,
					aq::ecs::HierarchicalTransformComponent,
					aq::ecs::DecalComponent>();

				auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
				tc->position.Set(0.0f, spawnY, 0.0f);   // プレイヤーと同じ足元
				tc->scale.Set(1.0f);

				auto* decal = entity.GetComponent<aq::ecs::DecalComponent>();
				decal->SetTexturePath("Assets/Terrain/rock.DDS");  // α無しなので四角く塗られる(可視確認用)
				decal->SetSize(aq::math::Vector3(4.0f, 20.0f, 4.0f)); // xz=範囲4, y=深度20(地面を貫く)
#ifdef AQ_DEBUG_IMGUI
				entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Decal");
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

			// ----- Prefab ツリー遅延生成テスト (PrefabSerializer + Prefab::Instantiate) -----
			// 単一 JSON プラン（root→child→grandchild、各ノードに Transform のみ）から
			// ツリーを遅延生成する Phase 3 の検証。
			//   - JSON → PrefabData（不変プラン）への変換（PrefabSerializer::FromJson）
			//   - 1 コマンドでツリー全体を遅延生成（shared_ptr 寿命ルール・§4.3）
			//   - components キー（"Transform"）→ TypeInfo 解決 + requiredWith(HTC) 自動補完
			//   - 各ノードの deserialize（local position 復元）
			//   - HTC 親子付け（root→child→grandchild）
			// HierarchicalTransformSystem は 1 フレーム後にワールド座標を更新するため、
			// onComplete では deserialize 済みの local position と親子構造のみ検証する。
			{
				const char* prefabJson = R"({
					"name": "PrefabTreeTest_Root",
					"components": { "Transform": { "position": [0, 1, 5] } },
					"children": [
						{
							"name": "PrefabTreeTest_Child",
							"components": { "Transform": { "position": [3, 0, 0] } },
							"children": [
								{
									"name": "PrefabTreeTest_Grandchild",
									"components": { "Transform": { "position": [0, 2, 0] } }
								}
							]
						}
					]
				})";

				aq::util::JsonValue planJson = aq::util::JsonParser::ParseString(prefabJson);
				aq::ecs::Prefab prefab = aq::ecs::PrefabSerializer::FromJson(planJson);
				EngineAssertMsg(prefab.IsValid(), "PrefabSerializer::FromJson: 有効な Prefab を返すこと");

				prefab.Instantiate(aq::ecs::EntityHandle(),
					[](aq::ecs::Entity root)
					{
						auto& ctx = aq::ecs::EntityContext::Get();

						auto* rootTc = root.GetComponent<aq::ecs::TransformComponent>();
						EngineAssertMsg(rootTc &&
							rootTc->position.x == 0.0f && rootTc->position.y == 1.0f && rootTc->position.z == 5.0f,
							"Prefab ツリー: root の local position が JSON から復元されること");

						auto rootChildren = ctx.GetChildren(root.GetHandle());
						EngineAssertMsg(rootChildren.size() == 1,
							"Prefab ツリー: root の子が 1 つ親子付けされていること");

						auto* childTc = ctx.GetComponent<aq::ecs::TransformComponent>(rootChildren[0]);
						EngineAssertMsg(childTc &&
							childTc->position.x == 3.0f && childTc->position.y == 0.0f && childTc->position.z == 0.0f,
							"Prefab ツリー: child の local position が復元されること");

						auto childChildren = ctx.GetChildren(rootChildren[0]);
						EngineAssertMsg(childChildren.size() == 1,
							"Prefab ツリー: child の子（grandchild）が親子付けされていること");

						auto* grandTc = ctx.GetComponent<aq::ecs::TransformComponent>(childChildren[0]);
						EngineAssertMsg(grandTc &&
							grandTc->position.x == 0.0f && grandTc->position.y == 2.0f && grandTc->position.z == 0.0f,
							"Prefab ツリー: grandchild の local position が復元されること");
					});
			}

			// ----- Prefab 生成 primitive テスト (RequestCreateEntityFromTypes) -----
			// 実行時 TypeInfo 集合からの遅延生成を検証する。
			//  - dedup    : 重複した TypeInfo を渡しても 1 つに集約される
			//  - 全構築   : onCreated 時点で全コンポーネントが GetComponent で取得できる
			//             （遅延 add→deserialize 問題の回避＝1 コマンド内で生成→構築→初期化）
			//  - 遅延生成 : 実体化は次フレームの ECS::Update→FlushCommands で行われる
			{
				std::vector<aq::ecs::TypeInfo> types = {
					aq::ecs::TypeInfo::Create<aq::ecs::TransformComponent>(),
					aq::ecs::TypeInfo::Create<aq::ecs::HierarchicalTransformComponent>(),
					aq::ecs::TypeInfo::Create<aq::ecs::BoxStaticMeshComponent>(),
					aq::ecs::TypeInfo::Create<aq::ecs::TransformComponent>(),   // 重複（dedup 検証用）
				};

				aq::ecs::EntityContext::Get().RequestCreateEntityFromTypes(
					std::move(types),
					[spawnY](aq::ecs::Entity entity)
					{
						// onCreated 時点で全コンポーネントが実体化済みであること
						auto* tc  = entity.GetComponent<aq::ecs::TransformComponent>();
						auto* htc = entity.GetComponent<aq::ecs::HierarchicalTransformComponent>();
						auto* box = entity.GetComponent<aq::ecs::BoxStaticMeshComponent>();
						EngineAssertMsg(tc && htc && box,
							"RequestCreateEntityFromTypes: 全コンポーネントが onCreated 時点で構築済みであること");
						if (tc) tc->position.Set(-3.0f, spawnY, 5.0f);
#ifdef AQ_DEBUG_IMGUI
						if (auto* tag = entity.GetComponent<aq::ecs::EntityDebugTag>())
							tag->SetName("PrefabPrimitiveTest");
#endif
					});
			}

			// ----- Reflect + JSON 往復テスト (JsonWrite/ReadVisitor) -----
			// TransformComponent を JSON へ書き出し→文字列化→パース→読み込みし、値が一致することを検証する。
			// レジストリ非依存（常時コンパイル）。Reflect が ImGui/JSON 共通基盤であることの確認。
			{
				aq::ecs::TransformComponent src;
				src.position.Set(1.0f, 2.0f, 3.0f);
				src.scale.Set(4.0f, 5.0f, 6.0f);
				src.rotation = aq::math::Quaternion::Identity;   // (0,0,0,1)

				aq::ecs::JsonWriteVisitor writer;
				src.Reflect(writer);
				const std::string json = aq::util::JsonSerializer::Stringify(writer.obj);

				aq::util::JsonValue parsed = aq::util::JsonParser::ParseString(json);
				aq::ecs::TransformComponent dst;   // 既定値から復元する
				aq::ecs::JsonReadVisitor reader(parsed);
				dst.Reflect(reader);

				EngineAssertMsg(
					dst.position.x == 1.0f && dst.position.y == 2.0f && dst.position.z == 3.0f &&
					dst.scale.x    == 4.0f && dst.scale.y    == 5.0f && dst.scale.z    == 6.0f &&
					dst.rotation.w == 1.0f,
					"Transform JSON 往復: position/scale/rotation が一致すること");
			}

			// ----- PrefabRegistry + Spawner テスト (Phase 4) -----
			// データ参照（文字列キー）→ ランタイム解決（PrefabId）→ System が遅延スポーン、を検証する。
			//   - Register/Resolve のキャッシュ一貫性（同一キー→同一 id、別キー→別 id）
			//   - Find が登録済み id で shared_ptr を返し、無効 id で nullptr を返す
			//   - SpawnerComponent を持つ実エンティティを配置し、SpawnSystem が毎フレーム遅延生成する
			{
				auto& reg = aq::ecs::PrefabRegistry::Get();

				// スポーン対象（小さな箱 1 個）の不変プランを in-memory 登録する。
				const char* payloadJson = R"({
					"name": "SpawnedBox",
					"components": { "Transform": { "position": [0, 0, 0] } }
				})";
				aq::util::JsonValue payloadPlan = aq::util::JsonParser::ParseString(payloadJson);
				aq::ecs::Prefab payloadPrefab   = aq::ecs::PrefabSerializer::FromJson(payloadPlan);

				const char* payloadKey = "mem://phase4_spawn_payload";
				aq::ecs::PrefabId id1 = reg.Register(payloadKey, payloadPrefab);
				aq::ecs::PrefabId id2 = reg.Resolve(payloadKey);   // キャッシュヒット（再ロードしない）
				EngineAssertMsg(id1.IsValid() && id1.value == id2.value,
					"PrefabRegistry: 同一キーは同一 PrefabId を返すこと");

				EngineAssertMsg(reg.Find(id1) != nullptr,
					"PrefabRegistry: 登録済み id で PrefabData を取得できること");
				EngineAssertMsg(reg.Find(aq::ecs::PrefabId{ 0 }) == nullptr,
					"PrefabRegistry: 無効 id では nullptr を返すこと");

				aq::ecs::PrefabId other = reg.Register("mem://phase4_other", payloadPrefab);
				EngineAssertMsg(other.IsValid() && other.value != id1.value,
					"PrefabRegistry: 別キーは別 PrefabId を割り当てること");

				// SpawnerComponent を持つエンティティを生成し、ランタイムスポーンを起動する。
				// 実体化は遅延（SpawnSystem→Instantiate→次 FlushCommands）で行われる。
				{
					std::vector<aq::ecs::TypeInfo> spawnerTypes = {
						aq::ecs::TypeInfo::Create<aq::ecs::TransformComponent>(),
						aq::ecs::TypeInfo::Create<aq::ecs::HierarchicalTransformComponent>(),
						aq::ecs::TypeInfo::Create<aq::ecs::SpawnerComponent>(),
					};
					aq::ecs::EntityContext::Get().RequestCreateEntityFromTypes(
						std::move(spawnerTypes),
						[payloadKey, spawnY](aq::ecs::Entity entity)
						{
							auto* spawner = entity.GetComponent<aq::ecs::SpawnerComponent>();
							EngineAssertMsg(spawner != nullptr,
								"Spawner: onCreated 時点で SpawnerComponent が構築済みであること");
							if (spawner) {
								spawner->prefabPath = payloadKey;   // 文字列正本（in-memory 登録済み）
								spawner->interval   = 1.0f;
								spawner->maxCount   = 3;            // 動的生成を 3 個に制限
							}
							if (auto* tc = entity.GetComponent<aq::ecs::TransformComponent>())
								tc->position.Set(6.0f, spawnY, 5.0f);
#ifdef AQ_DEBUG_IMGUI
							if (auto* tag = entity.GetComponent<aq::ecs::EntityDebugTag>())
								tag->SetName("Phase4_Spawner");
#endif
						});
				}
			}

			// ----- overrides 意味論テスト (Phase 6) -----
			// 子ノードの base components に overrides を適用する解決パス（PrefabSerializer の ApplyPatch）を検証する。
			//   - components の deep merge（position 上書き / scale 保持）
			//   - addedComponents（新規 Spawner 追加）
			//   - removedComponents（Decal 除去）
			// ネスト参照（"prefab"）のファイル展開も同じ ApplyPatch を共有する（ファイル IO を伴うため本テストは
			// インライン base + overrides で意味論のみを検証する）。
			{
				const char* overrideJson = R"({
					"name": "OverrideRoot",
					"components": { "Transform": { "position": [0, 0, 0] } },
					"children": [
						{
							"name": "OverrideChild",
							"components": {
								"Transform": { "position": [1, 1, 1], "scale": [2, 2, 2] },
								"Decal":     {}
							},
							"overrides": {
								"components":        { "Transform": { "position": [9, 9, 9] } },
								"addedComponents":   { "Spawner":   { "interval": 5 } },
								"removedComponents": [ "Decal" ]
							}
						}
					]
				})";

				aq::util::JsonValue plan = aq::util::JsonParser::ParseString(overrideJson);
				aq::ecs::Prefab prefab  = aq::ecs::PrefabSerializer::FromJson(plan);
				EngineAssertMsg(prefab.IsValid(), "Phase6: overrides 適用後も有効な Prefab を返すこと");

				prefab.Instantiate(aq::ecs::EntityHandle(),
					[](aq::ecs::Entity root)
					{
						auto& ctx = aq::ecs::EntityContext::Get();
						auto children = ctx.GetChildren(root.GetHandle());
						EngineAssertMsg(children.size() == 1, "Phase6: 子が 1 つ生成されること");
						if (children.empty()) return;

						const aq::ecs::EntityHandle child = children[0];

						auto* tc = ctx.GetComponent<aq::ecs::TransformComponent>(child);
						EngineAssertMsg(tc &&
							tc->position.x == 9.0f && tc->position.y == 9.0f && tc->position.z == 9.0f,
							"Phase6: overrides.components の position が deep merge で上書きされること");
						EngineAssertMsg(tc &&
							tc->scale.x == 2.0f && tc->scale.y == 2.0f && tc->scale.z == 2.0f,
							"Phase6: deep merge 後も base の scale が保持されること");

						EngineAssertMsg(ctx.GetComponent<aq::ecs::DecalComponent>(child) == nullptr,
							"Phase6: removedComponents の Decal が除去されること");

						auto* spawner = ctx.GetComponent<aq::ecs::SpawnerComponent>(child);
						EngineAssertMsg(spawner != nullptr,
							"Phase6: addedComponents の Spawner が追加されること");
						EngineAssertMsg(spawner && spawner->interval == 5.0f,
							"Phase6: addedComponents の Spawner.interval が復元されること");
					});
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
