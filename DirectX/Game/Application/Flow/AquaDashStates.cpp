#include "stdafx.h"
#include "AquaDashStates.h"
#include "UI/AquaDashScreens.h"
#include "GameInput.h"
#include "GameAction.h"
#include "ECS/SpeedCharacterComponentSystem.h"
#include "ECS/AutoCameraComponentSystem.h"
#include "Component/TerrainComponent.h"
#include "Component/AnimationComponentSystem.h"
#include "Terrain/HeightmapChunk.h"
#include "Level/LevelManager.h"
#include "Sound/SoundClip.h"
#include "Sound/SoundEngine.h"
#include "Util/ThreadPool.h"
#ifdef AQ_DEBUG_IMGUI
#include "ECS/EntityDebugTag.h"
#endif


namespace app
{
	namespace aquadash
	{
		namespace
		{
			/** メニュー項目 (ResultScreen の表示順と一致させる) */
			static constexpr int MENU_RETRY = 0;
			static constexpr int MENU_NEXT  = 1;
			static constexpr int MENU_TITLE = 2;

			// スティック上下をカーソル移動として拾うしきい値。
			static constexpr float STICK_EDGE = 0.5f;

			// ローディング表示の最低時間 (演出用) とワールド生成前の描画フレーム数。
			static constexpr float MIN_LOADING_SEC   = 0.6f;
			static constexpr int   WARMUP_FRAME_COUNT = 2;

			// アセットパス。
			static const char* STAGE_LIST_PATH  = "Assets/Stages/StageList.json";
			static const char* DECISION_SE_PATH = "Assets/Sound/Decision.wav";
			static const char* PLAYER_MODEL_PATH = "Assets/unityChan.tkm";
			static const char* PLAYER_IDLE_ANIM  = "Assets/animData/idle.tka";

			// unityChan.tkm はメートル基準でない (素のままだと約 6m)。世界は 1m=1.0 なので縮めて使う。
			static constexpr float PLAYER_MODEL_SCALE = 0.25f;


			// 決定 SE を再生する (クリップは GameFlow::Update で先読み済み)。
			void PlayDecisionSE()
			{
				if (aq::sound::SoundEngine::IsAvailable()) {
					auto clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(DECISION_SE_PATH);
					aq::sound::SoundEngine::Get().Play(clip, aq::sound::SoundBusId::SE);
				}
			}


			// 平坦地形 + プレイヤー + 自動カメラを生成する (ロード完了時に一度だけ)。
			// 生成した Entity はタイトル復帰時の破棄用に context.stageEntities へ積む。
			void CreateStageWorld(GameFlow& flow, const std::shared_ptr<stage::StageData>& stageData)
			{
				auto& ctx     = aq::ecs::EntityContext::Get();
				auto& context = flow.Context();
				context.stageEntities.clear();

				// コースの XZ 範囲を粗くサンプリングして地面サイズを決める。
				float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
				{
					const float total = stageData->spline.GetTotalLength();
					for (float d = 0.0f; d <= total; d += 10.0f) {
						const auto p = stageData->spline.Evaluate(d).position;
						if (p.x < minX) { minX = p.x; }
						if (p.x > maxX) { maxX = p.x; }
						if (p.z < minZ) { minZ = p.z; }
						if (p.z > maxZ) { maxZ = p.z; }
					}
				}

				// 地面 (平坦地形)。P1 は「平坦な地形 + スプライン走行」の最小構成。路面メッシュは後続フェーズ。
				{
					constexpr float MARGIN = 60.0f;
					const float extentX = maxX - minX + MARGIN * 2.0f;
					const float extentZ = maxZ - minZ + MARGIN * 2.0f;

					aq::terrain::HeightmapChunk::Desc desc;
					desc.heightmapPath = "Assets/Terrain/heightmap.png";
					desc.splatmapPath  = "Assets/Terrain/splatmap.png";
					desc.layerPaths[0] = "Assets/Terrain/grass.DDS";
					desc.layerPaths[1] = "Assets/Terrain/snow.DDS";
					desc.layerPaths[2] = "Assets/Terrain/rock.DDS";
					desc.resolution    = 128;
					desc.heightScale   = 0.0f;   // 平坦
					desc.terrainSize   = extentX > extentZ ? extentX : extentZ;
					desc.layerTiling   = desc.terrainSize / 5.0f;

					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent, aq::ecs::HierarchicalTransformComponent, aq::ecs::TerrainComponent>();
					auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
					tc->position.Set(minX - MARGIN, 0.0f, minZ - MARGIN);
					auto* terrain = entity.GetComponent<aq::ecs::TerrainComponent>();
					terrain->SetDesc(desc);
					terrain->GetChunk()->SetReceiveShadow(true);
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("StageGround");
#endif
					context.stageEntities.push_back(entity.GetHandle());
				}

				// メインカメラの基本設定。
				{
					aq::Camera* const mainCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
					mainCamera->SetNear(0.1f);
					mainCamera->SetViewportSize(
						static_cast<float>(aq::Engine::Get().GetRenderWidth()),
						static_cast<float>(aq::Engine::Get().GetRenderHeight()));
				}

				// プレイヤー (P1 は 1 人)。
				for (uint32_t i = 0; i < context.playerCount && i < MAX_PLAYER_COUNT; ++i)
				{
					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::SkeletalMeshComponent,
						aq::ecs::AnimationComponent,
						app::ecs::SpeedCharacterComponent,
						app::ecs::PlayerInputComponent>();

					auto* character = entity.GetComponent<app::ecs::SpeedCharacterComponent>();
					character->playerIndex = i;
					character->distance    = stageData->spawnDistance;
					character->lateral     = i < stageData->spawnLanes.size() ? stageData->spawnLanes[i] : 0.0f;

					entity.GetComponent<app::ecs::PlayerInputComponent>()->padIndex = i;

					const auto spawnFrame = stageData->spline.Evaluate(character->distance);
					auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
					tc->position = spawnFrame.position + spawnFrame.right * character->lateral;
					tc->scale.Set(PLAYER_MODEL_SCALE);

					auto* skelComp = entity.GetComponent<aq::ecs::SkeletalMeshComponent>();
					skelComp->SetShaderType(aq::graphics::SkeletalMesh::ShaderType::SkeletalPBRLit);
					skelComp->SetModelPath(PLAYER_MODEL_PATH);
					skelComp->GetSkeletalMesh()->SetCastShadow(true);
					skelComp->GetSkeletalMesh()->SetReceiveShadow(true);
					skelComp->GetSkeletalMesh()->SetReceivesDecal(false);

					auto* animComp = entity.GetComponent<aq::ecs::AnimationComponent>();
					animComp->AddAnimation(aqHash32("idle"), PLAYER_IDLE_ANIM);
					animComp->Play(aqHash32("idle"), true);
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("SpeedPlayer");
#endif
					context.playerHandles[i] = entity.GetHandle();
					context.stageEntities.push_back(entity.GetHandle());
				}
				flow.SetPlayerHandle(context.playerHandles[0]);   // 影の注視点用

				// 自動カメラ。
				{
					auto entity = ctx.CreateEntity<app::ecs::AutoCameraComponent>();
					auto* autoCam = entity.GetComponent<app::ecs::AutoCameraComponent>();
					autoCam->targetHandle = context.playerHandles[0];
					autoCam->cameraType   = aq::CameraType::Main;
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("AutoCamera");
#endif
					context.stageEntities.push_back(entity.GetHandle());
				}
			}


			// プレイヤーをスポーン状態へ戻す (「もう一度」のロードなし再開用)。
			void ResetPlayers(GameFlow& flow)
			{
				auto& context = flow.Context();
				const auto stageData = context.activeStage;
				if (!stageData) { return; }

				auto& ctx = aq::ecs::EntityContext::Get();
				for (uint32_t i = 0; i < context.playerCount && i < MAX_PLAYER_COUNT; ++i) {
					if (!ctx.IsValid(context.playerHandles[i])) { continue; }
					if (auto* character = ctx.GetComponent<app::ecs::SpeedCharacterComponent>(context.playerHandles[i])) {
						character->distance         = stageData->spawnDistance;
						character->lateral          = i < stageData->spawnLanes.size() ? stageData->spawnLanes[i] : 0.0f;
						character->height           = 0.0f;
						character->speed            = 0.0f;
						character->verticalVelocity = 0.0f;
						character->grounded         = true;
					}
				}
				context.playResult = PlayResult();

				// カメラは次フレームでスナップし直す。
				aq::ecs::Foreach<app::ecs::AutoCameraComponent>(
					[](const aq::ecs::Entity&, app::ecs::AutoCameraComponent* autoCam)
					{
						autoCam->initialized = false;
					});
			}


			// 生成したステージワールドと Level を破棄する (タイトル復帰時)。
			void DestroyStageWorld(GameFlow& flow)
			{
				auto& ctx     = aq::ecs::EntityContext::Get();
				auto& context = flow.Context();
				for (const auto& handle : context.stageEntities) {
					if (ctx.IsValid(handle)) {
						ctx.RequestDestroyEntity(handle);
					}
				}
				context.stageEntities.clear();
				for (auto& handle : context.playerHandles) {
					handle = aq::ecs::EntityHandle();
				}
				context.activeStage.reset();

				if (flow.LoadHandle().IsValid()) {
					aq::level::LevelManager::Get().Unload(flow.LoadHandle().GetLevelId());
					flow.SetLoadHandle(aq::level::LevelLoadHandle());
				}
			}
		}


		/**
		 * タイトル
		 */
		void TitleState::OnEnter(GameFlow& flow)
		{
			// ステージ一覧は初回のみ読む (小さな JSON なので同期でよい)。
			if (flow.Context().stageList.empty()) {
				flow.Context().stageList = stage::StageRegistry::LoadList(STAGE_LIST_PATH);
			}
		}


		void TitleState::OnUpdate(GameFlow& flow, const float /*dt*/)
		{
			if (!GameInput::Get().IsTriggered(GameAction::Confirm)) { return; }
			if (flow.Context().stageList.empty()) { return; }   // 一覧が無ければ開始できない

			PlayDecisionSE();

			flow.Context().playResult = PlayResult();
			aq::ui::UIContext::Get().Screens().Replace("Loading");
			flow.ChangeState(std::make_unique<LoadingState>());
		}


		/************************************/




		/**
		 * ローディング
		 */
		void LoadingState::OnEnter(GameFlow& flow)
		{
			phase_        = Phase::WarmUp;
			warmupFrames_ = 0;
			timer_        = 0.0f;

			const auto& context = flow.Context();
			const int index = context.selectedStageIndex;
			stagePath_ = context.stageList[index >= 0 && index < static_cast<int>(context.stageList.size()) ? index : 0].stagePath;
		}


		void LoadingState::OnUpdate(GameFlow& flow, const float dt)
		{
			timer_ += dt;

			switch (phase_)
			{
			case Phase::WarmUp:
				// ローディング画面を数フレーム描画してから重い処理へ (ドットアニメを止めないため)。
				if (++warmupFrames_ >= WARMUP_FRAME_COUNT) { phase_ = Phase::ParseStage; }
				break;

			case Phase::ParseStage:
			{
				// ステージ定義のパースはワーカースレッドで行う (CPU 処理のみ)。
				const std::string path = stagePath_;
				stageFuture_ = aq::util::ThreadPool::Get().Submit([path]()
					{
						return stage::StageData::LoadFromFile(path.c_str());
					});
				phase_ = Phase::WaitStage;
				break;
			}

			case Phase::WaitStage:
			{
				if (stageFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { break; }

				auto stageData = stageFuture_.get();
				EngineAssertMsg(stageData != nullptr, "ステージ定義の読み込みに失敗");
				if (!stageData) {
					// 読めない場合はタイトルへ戻す。
					aq::ui::UIContext::Get().Screens().Replace("AquaDashTitle");
					flow.ChangeState(std::make_unique<TitleState>());
					break;
				}

				flow.Context().activeStage = stageData;
				CreateStageWorld(flow, stageData);   // 重い同期処理 (1 フレームだけヒッチ)

				// 見た目 Level の非同期ロードを開始する。
				if (!stageData->levelPath.empty()) {
					flow.SetLoadHandle(aq::level::LevelManager::Get().LoadAsync(stageData->levelPath));
				}
				phase_ = Phase::Streaming;
				break;
			}

			case Phase::Streaming:
				if (flow.LoadHandle().IsDone() && timer_ >= MIN_LOADING_SEC)
				{
					aq::ui::UIContext::Get().Screens().Replace("AquaDashInGame");
					flow.ChangeState(std::make_unique<InGameState>());
				}
				break;
			}
		}


		/************************************/




		/**
		 * インゲーム
		 */
		void InGameState::OnEnter(GameFlow& flow)
		{
			elapsed_ = 0.0f;
			flow.Context().gameplayPaused = false;
			ResetPlayers(flow);
		}


		void InGameState::OnUpdate(GameFlow& flow, const float dt)
		{
			elapsed_ += dt;

			auto& context = flow.Context();
			const auto stageData = context.activeStage;
			if (!stageData) { return; }

			// ゴール / 落下判定 (P1 はプレイヤー 0 のみ。全員分の集計は P5)。
			auto& ctx = aq::ecs::EntityContext::Get();
			if (!ctx.IsValid(context.playerHandles[0])) { return; }
			const auto* character = ctx.GetComponent<app::ecs::SpeedCharacterComponent>(context.playerHandles[0]);
			if (!character) { return; }

			const bool goal = character->distance >= stageData->goalDistance;
			const bool fall = character->height   <  stageData->fallHeight;
			if (!goal && !fall) { return; }

			PlayResult& result  = context.playResult;
			result.cleared      = goal;
			result.clearTimeSec = elapsed_;

			aq::ui::UIContext::Get().Screens().Replace("AquaDashResult");
			flow.ChangeState(std::make_unique<ResultState>());
		}


		/************************************/




		/**
		 * リザルト
		 */
		void ResultState::OnEnter(GameFlow& flow)
		{
			cursor_     = 0;
			prevStickY_ = 0.0f;

			// 走行と判定を停止する。描画/アニメ/カメラは動き続けるため背景は生きたまま。
			flow.Context().gameplayPaused = true;

			// Replace 済みの最前面がリザルト画面。結果と初期カーソルを反映する。
			if (auto* screen = static_cast<ResultScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				const PlayResult& result = flow.Context().playResult;
				screen->SetResult(result.cleared, result.clearTimeSec);
				screen->SetCursor(cursor_);
			}
		}


		void ResultState::OnUpdate(GameFlow& flow, const float /*dt*/)
		{
			auto& input = GameInput::Get();

			// カーソル移動: W/S・D-Pad 上下のトリガ + 左スティック上下のエッジ検出。
			int move = 0;
			if (input.IsTriggered(GameAction::MoveBackward)) { move = +1; }
			if (input.IsTriggered(GameAction::MoveForward))  { move = -1; }

			const float stickY = input.GetStick(GameAction::Move).y;
			if (prevStickY_ < STICK_EDGE && stickY >= STICK_EDGE) {
				move = -1;   // 上入力
			} else if (prevStickY_ > -STICK_EDGE && stickY <= -STICK_EDGE) {
				move = +1;   // 下入力
			}
			prevStickY_ = stickY;

			if (move != 0)
			{
				cursor_ = (cursor_ + move + RESULT_MENU_COUNT) % RESULT_MENU_COUNT;
				if (auto* screen = static_cast<ResultScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
					screen->SetCursor(cursor_);
				}
			}

			if (!input.IsTriggered(GameAction::Confirm)) { return; }

			PlayDecisionSE();

			auto& screens = aq::ui::UIContext::Get().Screens();
			switch (cursor_)
			{
			case MENU_RETRY:
				// ロードなしで即再開 (InGameState::OnEnter がリセットする)。
				screens.Replace("AquaDashInGame");
				flow.ChangeState(std::make_unique<InGameState>());
				break;

			case MENU_NEXT:
				// ステージが 1 つの間は同じステージを再プレイ (複数化したら次インデックスをロードする)。
				screens.Replace("AquaDashInGame");
				flow.ChangeState(std::make_unique<InGameState>());
				break;

			case MENU_TITLE:
				DestroyStageWorld(flow);
				flow.Context().gameplayPaused = false;
				screens.Replace("AquaDashTitle");
				flow.ChangeState(std::make_unique<TitleState>());
				break;
			}
		}
	}
}
