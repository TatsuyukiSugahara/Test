#include "stdafx.h"
#include "GameFlow.h"
#include "GameInput.h"
#include "GameAction.h"
#include "Actor/StateMachine.h"

#include "Component/TerrainComponent.h"
#include "Component/AnimationComponentSystem.h"
#include "Component/ParticleComponentSystem.h"
#include "ECS/ECS.h"
#include "HID/Input.h"
#include "ECS/ActorComponentSystem.h"
#include "ECS/ActorSteeringComponentSystem.h"
#include "ECS/CameraSteeringComponentSystem.h"
#include "ECS/EntityContext.h"
#include "Terrain/HeightmapChunk.h"
#include "Level/LevelManager.h"
#include "UI/UIObject.h"
#include "UI/Component/UITextComponent.h"
#include "UI/Component/UIImageComponent.h"
#include "UI/Component/UITransformComponent.h"
#include "UI/Font/FontAssetCache.h"   // フォント事前ロード / 準備完了判定
#include "UI/Font/FontResource.h"
#include "UI/Font/FontAsset.h"
#include "Graphics/IShaderResourceView.h"
#include "Sound/SoundClip.h"
#include "Sound/SoundEngine.h"
#ifdef AQ_DEBUG_IMGUI
#include "ECS/EntityDebugTag.h"
#endif

namespace app
{
	namespace
	{
		// タイトルで押す非同期ロード対象。ECS の効果(大量エンティティ)確認用の箱 1000 個 Level。
		static const char*        STARTUP_LEVEL   = "Assets/Levels/Playground.level.json";
		// UI 文字は英数字のみ。ASCII 専用の小さな MSDF アトラス(512²/約74KB)を使い、
		// 起動時に即ロードできるようにする(全文字版 CorporateLogo は 8411 グリフ・4096²/12MB で重い)。
		static const char*        UI_FONT_PATH    = "Assets/Font/UI/atlas.json";
		// タイトルの筆文字「残刃 / 侍」用。日本語グリフ全部入りの重いアトラス(4096²)。
		// タイトルはこのフォントが主役なので、BootState で準備完了を待ってから表示する。
		static const char*        TITLE_FONT_PATH = "Assets/Font/CorporateLogo/atlas.json";
		// タイトルで決定(Space / パッド A)した瞬間に鳴らす SE。
		static const char*        DECISION_SE_PATH = "Assets/Sound/Decision.wav";
		static constexpr uint32_t LOAD_PER_FRAME  = 20;     // 1 フレームあたり生成数(ローディングを見せるため小さめ)
		static constexpr float    MIN_LOADING_SEC = 1.0f;   // ローディング表示の最低時間(演出用)
		static constexpr float    FONT_WAIT_MAX   = 15.0f;  // フォント準備待ちの安全上限(未完でも進む)

		// UI フォント(アトラステクスチャ含む)が描画可能な状態かを返す。
		// テキストはアトラス SRV がバインド可能(GetNativeHandle 非 null)になるまで描画されないため、
		// ローディング完了前にこれを待って "Now Loading" を確実に表示させる。
		bool IsUIFontReady()
		{
			auto fontRes = aq::ui::FontAssetCache::Get().Load(UI_FONT_PATH);
			if (!fontRes || !fontRes->IsCompleted()) return false;
			const aq::ui::FontAsset* fa = fontRes->GetFontAsset();
			if (!fa) return false;
			auto srv = fa->GetAtlasSRV();
			return srv && srv->GetNativeHandle() != nullptr;
		}


		// タイトル筆文字用フォント(CorporateLogo)が描画可能かを返す。IsUIFontReady と同様。
		bool IsTitleFontReady()
		{
			auto fontRes = aq::ui::FontAssetCache::Get().Load(TITLE_FONT_PATH);
			if (!fontRes || !fontRes->IsCompleted()) return false;
			const aq::ui::FontAsset* fa = fontRes->GetFontAsset();
			if (!fa) return false;
			auto srv = fa->GetAtlasSRV();
			return srv && srv->GetNativeHandle() != nullptr;
		}
	}


	// ── ゲーム状態(1 状態 1 クラス。増やすときはここにクラスを足すだけ) ─────────────

	namespace
	{
		// プレイ中。ワールドは ECS が駆動するので何もしない。
		// 将来ここでポーズ/ゲームオーバー→リザルトなどへ ChangeState する。
		class PlayingState : public IGameState
		{
		};


		// ローディング中。世界生成 + 非同期 Level ロードをフェーズに分けて進める。
		// ※ SetupWorld(地形/スケルタルメッシュ読込)は 1 フレームで完了する重い同期処理でヒッチする。
		//   これを OnEnter で即実行すると、その長フレーム中は UI が回らず "Now Loading" のドット
		//   アニメが進まない(=開始が遅れて見える)。そこで先にローディング画面を数フレーム描画・
		//   アニメさせてから(WarmUp)、重い処理を後続フレームに分けて回す。箱 1000 個は LoadAsync
		//   が LOAD_PER_FRAME ずつ分割生成する。
		class LoadingState : public IGameState
		{
		public:
			void OnEnter(GameFlow& /*flow*/) override
			{
				phase_        = Phase::WarmUp;
				warmupFrames_ = 0;
				timer_        = 0.0f;
			}

			void OnUpdate(GameFlow& flow, const float dt) override
			{
				timer_ += dt;

				switch (phase_)
				{
				case Phase::WarmUp:
					// ローディング画面を数フレーム描画・アニメさせてから重い同期処理へ。
					if (++warmupFrames_ >= WARMUP_FRAME_COUNT) { phase_ = Phase::SetupWorld; }
					break;

				case Phase::SetupWorld:
					// 地形/カメラ/ライト/プレイヤー生成(重い同期。ここだけ 1 フレーム分ヒッチ)。
					flow.SetupWorld();
					phase_ = Phase::StartStream;
					break;

				case Phase::StartStream:
					// 箱 1000 個の非同期 Level ロード開始(以降 Tick が分割生成)。
					flow.SetLoadHandle(
						aq::level::LevelManager::Get().LoadAsync(STARTUP_LEVEL, aq::level::LevelId(), LOAD_PER_FRAME));
					phase_ = Phase::Streaming;
					break;

				case Phase::Streaming:
				{
					// 完了条件: Level 生成完了 + 最低表示時間 + (フォント準備完了 or 安全上限)。
					const bool ready = flow.LoadHandle().IsDone()
					                && timer_ >= MIN_LOADING_SEC
					                && (IsUIFontReady() || timer_ >= FONT_WAIT_MAX);
					if (ready)
					{
						aq::ui::UIContext::Get().Screens().Pop();   // ローディングを閉じる
						flow.ChangeState(std::make_unique<PlayingState>());
					}
					break;
				}
				}
			}

		private:
			enum class Phase { WarmUp, SetupWorld, StartStream, Streaming };
			static constexpr int WARMUP_FRAME_COUNT = 2;   // 重い処理前にローディング画面を見せるフレーム数

			Phase phase_        = Phase::WarmUp;
			int   warmupFrames_ = 0;
			float timer_        = 0.0f;
		};


		// タイトル。決定(Space / パッド A)でローディング画面へ切替。世界生成/ロードは LoadingState::OnEnter で行う。
		class TitleState : public IGameState
		{
		public:
			void OnUpdate(GameFlow& flow, const float /*dt*/) override
			{
				auto& screens = aq::ui::UIContext::Get().Screens();
				auto* title   = static_cast<TitleScreen*>(screens.Top());   // 表示中はタイトルが最前面

				// 未押下: 決定入力を待ち、押されたらフラッシュ演出を開始する。
				if (!startRequested_)
				{
					if (!GameInput::Get().IsTriggered(GameAction::Confirm)) { return; }
					startRequested_ = true;
					if (title) { title->RequestStart(); }
					// 決定 SE を再生(クリップは GameFlow::Update で先読み済み)。
					if (aq::sound::SoundEngine::IsAvailable()) {
						auto clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(DECISION_SE_PATH);
						aq::sound::SoundEngine::Get().Play(clip, aq::sound::SoundBusId::SE);
					}
					return;
				}

				// フラッシュが十分進んだらローディングへ切替(世界生成/ロードは LoadingState で行う)。
				if (!title || title->IsStartFlashDone())
				{
					screens.Replace("Loading");
					flow.ChangeState(std::make_unique<LoadingState>());
				}
			}

		private:
			bool startRequested_ = false;
		};


		// 起動時。重いフォントアトラス(4096²)の準備が整うまで黒いローディング画面で待ち、完了後にタイトルへ。
		// これによりタイトル/ローディングのテキストが確実に表示される(未完のまま出て文字が出ないのを防ぐ)。
		class BootState : public IGameState
		{
		public:
			void OnEnter(GameFlow& /*flow*/) override
			{
				aq::ui::UIContext::Get().Screens().Push("Loading");   // 準備中は黒背景
			}

			void OnUpdate(GameFlow& flow, const float dt) override
			{
				timer_ += dt;
				// タイトルは筆文字が主役。ASCII とタイトル用フォントの両方が整うまで待つ(安全上限あり)。
				if ((IsUIFontReady() && IsTitleFontReady()) || timer_ >= FONT_WAIT_MAX)
				{
					aq::ui::UIContext::Get().Screens().Replace("Title");
					flow.ChangeState(std::make_unique<TitleState>());
				}
			}

		private:
			float timer_ = 0.0f;
		};
	}


	// ── ローディング画面(ドット増減) ────────────────────────────────────────────

	void LoadingScreen::OnEnter()
	{
		dotTimer_ = 0.0f;
		dotCount_ = 0;
		dotDir_   = 1;
	}


	void LoadingScreen::OnUpdate(const float dt)
	{
		// 0.3 秒ごとにドット数を 0..3 で往復させる。
		dotTimer_ += dt;
		if (dotTimer_ >= 0.3f) {
			dotTimer_ = 0.0f;
			dotCount_ += dotDir_;
			if (dotCount_ >= 3)      { dotCount_ = 3; dotDir_ = -1; }
			else if (dotCount_ <= 0) { dotCount_ = 0; dotDir_ =  1; }
		}

		if (auto* obj = Resolve(FindHandle("LoadingText"))) {
			if (auto* text = obj->GetComponent<aq::ui::UITextComponent>()) {
				std::string s = "Now Loading";
				for (int i = 0; i < dotCount_; ++i) s += ".";
				text->content = s;
				text->color   = { 1.0f, 1.0f, 1.0f, 1.0f };   // 黒背景に白文字(TextStyle に依らず可視化)
			}
		}
	}


	// ── タイトル画面(案A「墨」) ────────────────────────────────────────────────

	namespace
	{
		// 経過 t[s] を区間 [delay, delay+dur] で 0..1 に正規化し、EaseOut(cubic)を掛けた値。
		// HTML 版 cubic-bezier(.2,.7,.2,1) の減速カーブを近似する。
		float EaseOutSeg(const float t, const float delay, const float dur)
		{
			if (dur <= 0.0f) { return t >= delay ? 1.0f : 0.0f; }
			float p = (t - delay) / dur;
			if (p <= 0.0f) { return 0.0f; }
			if (p >= 1.0f) { return 1.0f; }
			const float inv = 1.0f - p;
			return 1.0f - inv * inv * inv;
		}


		// テキストの不透明度。a==0 はスタイル既定色扱い(不可視にできない)になるため最小値でクランプ。
		void SetTextAlpha(aq::ui::UIObject* obj, const float a)
		{
			if (!obj) { return; }
			if (auto* t = obj->GetComponent<aq::ui::UITextComponent>()) {
				t->color.w = a < 0.02f ? 0.02f : a;
			}
		}


		// 画像の不透明度(a==0 は完全透明で問題なし)。
		void SetImageAlpha(aq::ui::UIObject* obj, const float a)
		{
			if (!obj) { return; }
			if (auto* i = obj->GetComponent<aq::ui::UIImageComponent>()) {
				i->color.w = a;
			}
		}


		// Transform の等方スケール(画像矩形の拡縮に使う。テキストのグリフは fontSize/scale 側で拡縮)。
		void SetUniformScale(aq::ui::UIObject* obj, const float s)
		{
			if (!obj) { return; }
			if (auto* t = obj->GetComponent<aq::ui::UITransformComponent>()) {
				t->localScale.x = s;
				t->localScale.y = s;
			}
		}
	}


	void TitleScreen::OnEnter()
	{
		elapsed_      = 0.0f;
		startPressed_ = false;
		flashTime_    = 0.0f;

		// 名前解決は入場時に一度だけ行い、以降 OnUpdate はキャッシュしたポインタを使う。
		eyebrow_   = Resolve(FindHandle("Eyebrow"));
		kanji_     = Resolve(FindHandle("Kanji"));
		sealOuter_ = Resolve(FindHandle("SealOuter"));
		sealInner_ = Resolve(FindHandle("SealInner"));
		sealChar_  = Resolve(FindHandle("SealChar"));
		romaji_    = Resolve(FindHandle("Romaji"));
		press_     = Resolve(FindHandle("Press"));
		flash_     = Resolve(FindHandle("Flash"));
	}


	void TitleScreen::OnUpdate(const float dt)
	{
		elapsed_ += dt;
		const float t = elapsed_;

		// 筆文字「残刃」: にじみ登場(scale 1.35→1.0 / alpha 0→1)。delay 0.15 / dur 1.5
		if (kanji_)
		{
			const float p = EaseOutSeg(t, 0.15f, 1.5f);
			if (auto* txt = kanji_->GetComponent<aq::ui::UITextComponent>()) {
				txt->scale   = 1.35f - 0.35f * p;
				txt->color.w = p < 0.02f ? 0.02f : p;
			}
		}

		// eyebrow "THE LAST BLADE": フェードイン。delay 1.1 / dur 1.2
		SetTextAlpha(eyebrow_, EaseOutSeg(t, 1.1f, 1.2f));

		// 落款「侍」: せり出し(scale 0.4→1.0 / alpha 0→1)。delay 1.3 / dur 1.4
		{
			const float p = EaseOutSeg(t, 1.3f, 1.4f);
			const float s = 0.4f + 0.6f * p;
			SetUniformScale(sealOuter_, s);
			SetUniformScale(sealInner_, s);
			SetImageAlpha(sealOuter_, p);
			SetImageAlpha(sealInner_, p);
			if (sealChar_) {
				if (auto* txt = sealChar_->GetComponent<aq::ui::UITextComponent>()) {
					txt->scale   = s;   // 文字はフォントスケールで拡縮(矩形スケールではグリフが変わらない)
					txt->color.w = p < 0.02f ? 0.02f : p;
				}
			}
		}

		// romaji "ZANJIN": 下からせり上がりつつフェード(y 357→315)。delay 1.2 / dur 1.0
		{
			const float p = EaseOutSeg(t, 1.2f, 1.0f);
			if (romaji_) {
				if (auto* tr = romaji_->GetComponent<aq::ui::UITransformComponent>()) {
					tr->localPosition.y = 357.0f - 42.0f * p;
				}
			}
			SetTextAlpha(romaji_, p);
		}

		// PRESS ANY BUTTON: 1.8s でフェードイン後、周期 1.6s で点滅(高→低のホールド型)。
		{
			float a;
			if (t < 1.8f) {
				a = 0.0f;
			} else if (t < 2.0f) {
				a = (t - 2.0f + 0.2f) / 0.2f;   // 1.8→2.0 で 0→1
			} else {
				// fmod を使わずに位相を求める(cmath 非依存)。
				const float x  = t - 2.0f;
				const int   n  = static_cast<int>(x / 1.6f);
				const float ph = (x - static_cast<float>(n) * 1.6f) / 1.6f;
				a = ph < 0.5f ? 1.0f : 0.12f;
			}
			SetTextAlpha(press_, a);
		}

		// 決定後の白フラッシュ(alpha 0.85→0 / 0.5s)。
		if (startPressed_)
		{
			flashTime_ += dt;
			float fa = 0.85f * (1.0f - flashTime_ / 0.5f);
			if (fa < 0.0f) { fa = 0.0f; }
			SetImageAlpha(flash_, fa);
		}
	}


	void TitleScreen::RequestStart()
	{
		startPressed_ = true;
		flashTime_    = 0.0f;
	}


	bool TitleScreen::IsStartFlashDone() const
	{
		return startPressed_ && flashTime_ >= 0.35f;
	}


	// ── GameFlow ─────────────────────────────────────────────────────────────

	GameFlow* GameFlow::instance_ = nullptr;


	void GameFlow::Create()
	{
		if (!instance_) instance_ = new GameFlow();
	}


	void GameFlow::Release()
	{
		if (instance_) { delete instance_; instance_ = nullptr; }
	}


	void GameFlow::Initialize()
	{
		auto& screens = aq::ui::UIContext::Get().Screens();
		screens.Register<TitleScreen>("Title",     "Assets/UI/Title.screen.json");
		screens.Register<LoadingScreen>("Loading", "Assets/UI/Loading.screen.json");

		// フォント準備を待ってからタイトルを出す(BootState)。テキストを確実に表示するため。
		current_ = std::make_unique<BootState>();
		current_->OnEnter(*this);
	}


	void GameFlow::ChangeState(std::unique_ptr<IGameState> next)
	{
		pending_ = std::move(next);
	}


	void GameFlow::Update(const float dt)
	{
		// UI 画像テクスチャの事前ロード(初回のみ)。リソースバンク登録は OnRegister(OnInitialize より後)なので、
		// 最初の Update 時点で行う。未ロード中はバインドがスキップされ表示が遅れるため、
		// 事前にキャッシュして決定時に即座に黒背景を出せるようにする。
		if (!preloaded_)
		{
			aq::res::ResourceManager::Get().Load<aq::res::GPUResource>("Assets/Terrain/rock.png");
			aq::res::ResourceManager::Get().Load<aq::res::GPUResource>("Assets/Character/Character.png");
			// UI フォント(小さな ASCII アトラス)を先読み。テキストはアトラス完了まで描画されないため。
			aq::ui::FontAssetCache::Get().Load(UI_FONT_PATH);
			// タイトル筆文字用フォント(CorporateLogo)も先読み。BootState が準備完了を待つ。
			aq::ui::FontAssetCache::Get().Load(TITLE_FONT_PATH);
			// タイトルの単色塗り(和紙/落款/フラッシュ)に使う白テクスチャ。
			aq::res::ResourceManager::Get().Load<aq::res::GPUResource>("Assets/UI/Textures/white.png");
			// 決定 SE を先読み(タイトルで押した瞬間に即鳴らせるように)。
			aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(DECISION_SE_PATH);
			preloaded_ = true;
		}

		// 保留中の遷移を境界で適用する(状態の OnUpdate 内から ChangeState しても安全)。
		if (pending_)
		{
			if (current_) current_->OnExit(*this);
			current_ = std::move(pending_);
			current_->OnEnter(*this);
		}

		if (current_) current_->OnUpdate(*this, dt);

		// デバッグ: Enter キーでシーン内のパーティクルを頭から再生し直す。
		// FX_Explosion は一発 (非ループ) なので、インゲームで動作を繰り返し確認するのに使う。
		if (aq::hid::IsKeyTriggered(aq::hid::KeyBoardType::Enter))
		{
			aq::ecs::Foreach<aq::ecs::ParticleEmitterComponent>(
				[](const aq::ecs::Entity&, aq::ecs::ParticleEmitterComponent* emitter)
				{
					emitter->Restart();
				});
		}
	}


	aq::math::Vector3 GameFlow::GetFocusPosition() const
	{
		auto& ctx = aq::ecs::EntityContext::Get();
		if (ctx.IsValid(playerHandle_))
		{
			if (auto* htc = ctx.GetComponent<aq::ecs::HierarchicalTransformComponent>(playerHandle_))
				return htc->transform.position;
		}
		return aq::math::Vector3(0.0f, 0.0f, 0.0f);
	}


	// 旧 BattleScene::Initialize の 3D 世界セットアップ(地形/カメラ/ライト/プレイヤー/ステアリング)を移設。
	// Prefab/ECS の検証テスト・海・デカール・地形ペインタは削除。箱は非同期 Level で別途生成する。
	void GameFlow::SetupWorld()
	{
		auto& ctx = aq::ecs::EntityContext::Get();

		// 地形
		aq::ecs::TerrainComponent* terrainComp = nullptr;
		{
			aq::terrain::HeightmapChunk::Desc desc;
			desc.heightmapPath = "Assets/Terrain/heightmap.png";
			desc.splatmapPath  = "Assets/Terrain/splatmap.png";
			desc.layerPaths[0] = "Assets/Terrain/grass.DDS";
			desc.layerPaths[1] = "Assets/Terrain/snow.DDS";
			desc.layerPaths[2] = "Assets/Terrain/rock.DDS";
			desc.resolution    = 128;
			desc.heightScale   = 10.0f;
			desc.terrainSize   = 100.0f;
			desc.layerTiling   = 20.0f;

			auto entity = ctx.CreateEntity<
				aq::ecs::TransformComponent, aq::ecs::HierarchicalTransformComponent, aq::ecs::TerrainComponent>();
			auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
			tc->position.Set(-50.0f, 0.0f, -50.0f);
			tc->scale.Set(1.0f);
			terrainComp = entity.GetComponent<aq::ecs::TerrainComponent>();
			terrainComp->SetDesc(desc);
			terrainComp->GetChunk()->SetReceiveShadow(true);
#ifdef AQ_DEBUG_IMGUI
			entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Terrain");
#endif
		}

		// world(0,0) = terrain local(50,50)(XZ オフセット -50 適用後)
		const float spawnY = terrainComp->GetChunk()->GetHeight(50.0f, 50.0f);

		// メインカメラ(位置/注視点は CameraSteeringSystem が管理) + オフスクリーンカメラ + ライト
		aq::Camera* const mainCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
		mainCamera->SetNear(0.01f);
		mainCamera->SetViewportSize(
			static_cast<float>(aq::Engine::Get().GetRenderWidth()),
			static_cast<float>(aq::Engine::Get().GetRenderHeight()));

		aq::Camera* offscreenCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Offscreen);
		offscreenCamera->SetPosition(aq::math::Vector3(0.0f, spawnY + 5.0f, -15.0f));
		offscreenCamera->SetTarget(aq::math::Vector3(0.0f, spawnY, 5.0f));
		offscreenCamera->SetNear(0.01f);

		aq::graphics::LightManager::Get().SetDirectionalColor(aq::math::Vector3(1.0f, 0.6f, 0.6f));

		// プレイヤー(スケルタルメッシュ + Idle アニメ + ステートマシン)
		aq::ecs::EntityHandle targetHandle;
		{
			auto entity = ctx.CreateEntity<
				aq::ecs::TransformComponent,
				aq::ecs::HierarchicalTransformComponent,
				aq::ecs::SkeletalMeshComponent,
				aq::ecs::AnimationComponent,
				app::ecs::StateMachineComponent>();

			targetHandle  = entity.GetHandle();
			playerHandle_ = targetHandle;

			auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
			tc->position.Set(0.0f, spawnY, 0.0f);
			tc->scale.Set(1.0f);

			auto* skelComp = entity.GetComponent<aq::ecs::SkeletalMeshComponent>();
			skelComp->SetShaderType(aq::graphics::SkeletalMesh::ShaderType::SkeletalPBRLit);
			skelComp->SetModelPath("Assets/unityChan.tkm");
			skelComp->GetSkeletalMesh()->SetCastShadow(true);
			skelComp->GetSkeletalMesh()->SetReceiveShadow(true);
			skelComp->GetSkeletalMesh()->SetReceivesDecal(false);

			auto* animComp = entity.GetComponent<aq::ecs::AnimationComponent>();
			animComp->AddAnimation(aqHash32("idle"), "Assets/animData/idle.tka");
			animComp->Play(aqHash32("idle"), true);

			auto* sm = entity.GetComponent<app::ecs::StateMachineComponent>();
			sm->GetStateMachine()->AddState<app::actor::IdleState>(aqHash32("Idle"));
			sm->GetStateMachine()->AddState<app::actor::MoveState>(aqHash32("Move"));
			sm->GetStateMachine()->RequestStateID(aqHash32("Idle"));
			sm->GetStateMachine()->SetTargetHandle(targetHandle);
#ifdef AQ_DEBUG_IMGUI
			entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Player");
#endif
		}

		// キャラクターステアリング(入力 → 移動)
		{
			auto entity = ctx.CreateEntity<app::ecs::CharacterSteeringComponent>();
			entity.GetComponent<app::ecs::CharacterSteeringComponent>()->SetTarget(targetHandle);
#ifdef AQ_DEBUG_IMGUI
			entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("CharacterSteering");
#endif
		}

		// カメラステアリング(プレイヤー追従 TPS)
		{
			auto entity = ctx.CreateEntity<app::ecs::CameraSteeringComponent>();
			auto* cam = entity.GetComponent<app::ecs::CameraSteeringComponent>();
			cam->TrackEntity(targetHandle, aq::math::Vector3(0.0f, 1.5f, 0.0f));
			cam->SetManualView(0.0f, 20.0f, 10.0f);
			cam->cameraType = aq::CameraType::Main;
#ifdef AQ_DEBUG_IMGUI
			entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("CameraTracking");
#endif
		}

		// カメラエフェクト(シェイク等の加算)
		{
			auto entity = ctx.CreateEntity<app::ecs::CameraEffectComponent>();
			entity.GetComponent<app::ecs::CameraEffectComponent>()->cameraType = aq::CameraType::Main;
#ifdef AQ_DEBUG_IMGUI
			entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("CameraEffect");
#endif
		}
	}
}
