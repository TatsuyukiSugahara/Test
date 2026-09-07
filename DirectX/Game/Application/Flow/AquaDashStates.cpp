#include "stdafx.h"
#include "AquaDashStates.h"
#include "UI/AquaDashScreens.h"
#include "GameInput.h"
#include "GameAction.h"
#include "ECS/SpeedCharacterComponentSystem.h"
#include "ECS/AutoCameraComponentSystem.h"
#include "ECS/CoinComponentSystem.h"
#include "Component/TerrainComponent.h"
#include "Component/AnimationComponentSystem.h"
#include "Component/ParticleComponentSystem.h"
#include <cstdio>
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

			// コイン取得エフェクト (常駐エミッタを移動+Restart で使い回す)。
			static const char* COLLECT_FX_PATH = "Assets/Particle/FX_Explosion.particle";


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
					mainCamera->SetFar(3000.0f);   // 長距離コースの見通し (既定 1000 だと途中で切れる)
					mainCamera->SetViewportSize(
						static_cast<float>(aq::Engine::Get().GetRenderWidth()),
						static_cast<float>(aq::Engine::Get().GetRenderHeight()));
				}

				// ミニマップの正規化パラメータ (コース XZ 範囲 → 0-1 への変換に使う)。
				{
					const float extentX = maxX - minX;
					const float extentZ = maxZ - minZ;
					context.minimapCenterXZ   = aq::math::Vector2((minX + maxX) * 0.5f, (minZ + maxZ) * 0.5f);
					context.minimapHalfExtent = (extentX > extentZ ? extentX : extentZ) * 0.5f + 40.0f;
				}

				// 路面タイル (スプラインに沿った薄い箱)。走行時の路面の見た目と、
				// ミニマップ (俯瞰) に映るコース形状を兼ねる。ループでもタイル姿勢が路面に追従する。
				{
					// 箱はフラスタムカリング対象 (P8 でバウンディング付与)。画面外タイルは描画されない。
					// 20m 間隔 (約360枚) は生成コストとミニマップ形状のバランスで維持。
					constexpr float TILE_SPACING = 20.0f;
					const float total = stageData->spline.GetTotalLength();
					for (float d = 0.0f; d < total; d += TILE_SPACING)
					{
						const auto frame = stageData->spline.Evaluate(d);
						auto entity = ctx.CreateEntity<
							aq::ecs::TransformComponent,
							aq::ecs::HierarchicalTransformComponent,
							aq::ecs::BoxStaticMeshComponent>();
						auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
						// 地形面 (y=0) より上面がわずかに出るよう -0.10 (厚み 0.3 → 上面 +0.05)。
						// 深く沈めると平坦地形に埋まって見えなくなる。
						tc->position = frame.position - frame.up * 0.1f;
						tc->scale.Set(stageData->width, 0.3f, TILE_SPACING * 1.02f);
						tc->rotation = frame.ToRotation();
						// 路面は青みグレー (仮アセット。専用モデル導入までの色分け)。
						entity.GetComponent<aq::ecs::BoxStaticMeshComponent>()->SetColor(
							aq::math::Vector4(0.30f, 0.34f, 0.42f, 1.0f));
						context.stageEntities.push_back(entity.GetHandle());
					}
				}

				// プレイヤー。
				{
					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::SkeletalMeshComponent,
						aq::ecs::AnimationComponent,
						app::ecs::SpeedCharacterComponent,
						app::ecs::PlayerInputComponent,
						app::ecs::PlayerScoreComponent>();

					auto* character = entity.GetComponent<app::ecs::SpeedCharacterComponent>();
					character->distance = stageData->spawnDistance;
					character->lateral  = stageData->spawnLanes.empty() ? 0.0f : stageData->spawnLanes[0];

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
					context.playerHandle = entity.GetHandle();
					context.stageEntities.push_back(entity.GetHandle());
				}
				flow.SetPlayerHandle(context.playerHandle);   // 影の注視点用

				// 自動カメラ。
				{
					auto entity = ctx.CreateEntity<app::ecs::AutoCameraComponent>();
					auto* autoCam = entity.GetComponent<app::ecs::AutoCameraComponent>();
					autoCam->targetHandle = context.playerHandle;
					autoCam->cameraType   = aq::CameraType::Main;
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("AutoCamera");
#endif
					context.stageEntities.push_back(entity.GetHandle());
				}

				// コイン (スプライン座標 → ワールドへ焼き込み。判定と回転は CoinSystem)。
				for (const auto& placement : stageData->coins)
				{
					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::BoxStaticMeshComponent,
						app::ecs::CoinComponent>();

					const auto frame = stageData->spline.Evaluate(placement.distance);
					auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
					tc->position = frame.position + frame.right * placement.lateral + frame.up * placement.height;
					tc->scale.Set(0.8f, 0.8f, 0.15f);   // 薄い箱をコインに見立てる (専用モデルは未導入)
					// コインはゴールド (仮アセット)。
					entity.GetComponent<aq::ecs::BoxStaticMeshComponent>()->SetColor(
						aq::math::Vector4(1.00f, 0.82f, 0.15f, 1.0f));

					auto* coin = entity.GetComponent<app::ecs::CoinComponent>();
					coin->distance     = placement.distance;
					coin->baseRotation = frame.ToRotation();
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Coin");
#endif
					context.stageEntities.push_back(entity.GetHandle());
				}

				// コイン取得エフェクトの常駐エミッタ (取得時に移動して Restart する)。
				{
					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::ParticleEmitterComponent>();
					auto* emitter = entity.GetComponent<aq::ecs::ParticleEmitterComponent>();
					emitter->SetAsset(COLLECT_FX_PATH);
					emitter->SetPlaying(false);   // 生成直後に鳴らない
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("CollectFX");
#endif
					context.collectFxHandle = entity.GetHandle();
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
				if (ctx.IsValid(context.playerHandle)) {
					if (auto* character = ctx.GetComponent<app::ecs::SpeedCharacterComponent>(context.playerHandle)) {
						character->distance         = stageData->spawnDistance;
						character->lateral          = stageData->spawnLanes.empty() ? 0.0f : stageData->spawnLanes[0];
						character->height           = 0.0f;
						character->speed            = 0.0f;
						character->verticalVelocity = 0.0f;
						character->grounded         = true;
						character->fallen           = false;
						character->worldVelocity    = aq::math::Vector3(0.0f, 0.0f, 0.0f);
					}
					if (auto* score = ctx.GetComponent<app::ecs::PlayerScoreComponent>(context.playerHandle)) {
						score->coinCount = 0;
						score->fallCount = 0;
					}
				}
				context.playResult = PlayResult();

				// コインを全復活させる (取得済みフラグと表示を戻す)。
				app::ecs::CoinSystem::ReactivateAll();

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
				context.playerHandle    = aq::ecs::EntityHandle();
				context.collectFxHandle = aq::ecs::EntityHandle();
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

			// 選択中ステージ名をタイトルへ反映する。
			const auto& list = flow.Context().stageList;
			if (!list.empty()) {
				const int index = aq::math::Clamp(flow.Context().selectedStageIndex, 0, static_cast<int>(list.size()) - 1);
				if (auto* screen = static_cast<TitleScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
					char buf[64];
					std::snprintf(buf, sizeof(buf), "STAGE %02d    %s", index + 1, list[index].name.c_str());
					screen->SetStageName(buf);
				}
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

			// ミニマップ: コース形状をスプラインから等間隔サンプリングし、UI の点列として描く。
			if (auto* screen = static_cast<InGameScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				std::vector<aq::math::Vector2> uvPoints;
				const auto& context   = flow.Context();
				const auto  stageData = context.activeStage;
				if (stageData && stageData->spline.IsValid() && context.minimapHalfExtent > 1.0f)
				{
					constexpr int SAMPLE_COUNT = 160;
					const float span  = context.minimapHalfExtent * 2.0f;
					const float total = stageData->spline.GetTotalLength();
					uvPoints.reserve(SAMPLE_COUNT + 1);
					for (int i = 0; i <= SAMPLE_COUNT; ++i)
					{
						const auto position =
							stageData->spline.Evaluate(total * static_cast<float>(i) / SAMPLE_COUNT).position;
						uvPoints.push_back(aq::math::Vector2(
							0.5f + (position.x - context.minimapCenterXZ.x) / span,
							0.5f - (position.z - context.minimapCenterXZ.y) / span));
					}
				}
				screen->SetMinimapCourse(uvPoints);
			}
		}


		void InGameState::OnUpdate(GameFlow& flow, const float dt)
		{
			elapsed_ += dt;

			auto& context = flow.Context();
			const auto stageData = context.activeStage;
			if (!stageData) { return; }

			auto& ctx = aq::ecs::EntityContext::Get();
			if (!ctx.IsValid(context.playerHandle)) { return; }
			const auto* character = ctx.GetComponent<app::ecs::SpeedCharacterComponent>(context.playerHandle);
			if (!character) { return; }

			// HUD 更新 (時間 / コイン / 速度 / ミニマップマーカー)。
			const auto* score    = ctx.GetComponent<app::ecs::PlayerScoreComponent>(context.playerHandle);
			const auto* playerTc = ctx.GetComponent<aq::ecs::TransformComponent>(context.playerHandle);
			if (auto* screen = static_cast<InGameScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				screen->SetHUD(elapsed_, score ? score->coinCount : 0, character->speed * 3.6f);

				// 俯瞰カメラは 画面右=+X / 画面上=+Z。UI の v は下+なので Z を反転する。
				if (playerTc && context.minimapHalfExtent > 1.0f) {
					const float span = context.minimapHalfExtent * 2.0f;
					const float u = 0.5f + (playerTc->position.x - context.minimapCenterXZ.x) / span;
					const float v = 0.5f - (playerTc->position.z - context.minimapCenterXZ.y) / span;
					screen->SetMinimapMarker(u, v);
				}
			}

			// ゴール / 落下判定。
			// 落下は「路面相対 height がしきい値未満」または「ループ脱落後に地面高さまで落ちた」。
			const bool  goal     = character->distance >= stageData->goalDistance;
			const bool  fall     = character->height < stageData->fallHeight
			                    || (character->fallen && playerTc && playerTc->position.y < 0.5f);
			if (!goal && !fall) { return; }

			PlayResult& result  = context.playResult;
			result.cleared      = goal;
			result.clearTimeSec = elapsed_;
			result.coinCount    = score ? score->coinCount : 0;

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
				const auto&       context = flow.Context();
				const PlayResult& result  = context.playResult;

				// ランクはクリア時のみ (設計 03: ゲームオーバーはランクなし)。
				std::string rank;
				if (result.cleared && context.activeStage) {
					rank = context.activeStage->CalcRank(result.coinCount, result.clearTimeSec);
				}
				screen->SetResult(result.cleared, result.clearTimeSec, result.coinCount, rank.c_str());
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
