#include "aq.h"
#include "Engine.h"
#include "RenderConfig.h"
#include "Core/IApplication.h"
#include "Platform/IPlatform.h"
#include "Platform/PlatformBudget.h"
#include "Util/ThreadPool.h"
#include "Physics/PhysicsBackend.h"
#include "Sound/SoundEngine.h"
#include "Sound/SoundBackend.h"
#include "Sound/Authoring/AudioDirector.h"
#ifdef ENGINE_GRAPHICS_D3D11
#include "Graphics/D3D11/D3D11GraphicsDeviceImpl.h"
#elif defined(ENGINE_GRAPHICS_D3D12)
#include "Graphics/D3D12/D3D12GraphicsDeviceImpl.h"
#elif defined(ENGINE_GRAPHICS_VULKAN)
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#endif


namespace aq
{
	Engine* Engine::instance_ = nullptr;


	Engine::Engine()
		: platform_(nullptr)
		, window_()
		, renderContext_()
		, currentMainRenderTarget_(0)
		, screenWidth_(0)
		, screenHeight_(0)
		, renderWidth_(0)
		, renderHeight_(0)
		, application_(nullptr)
	{
	}


	Engine::~Engine()
	{
	}


	bool Engine::Initialize(const InitializeParameter& initializeParameter)
	{
		platform_ = initializeParameter.platform;
		EngineAssert(platform_);

		// メモリマネージャを最初に初期化することで、ウィンドウ・グラフィクス初期化中の
		// new/delete もエンジンアロケータ管理下に置く。
		aq::memory::MemoryManager::Initialize(initializeParameter.memoryConfig);

		// Bullet allocator hook は MemoryManager 直後、かつ Bullet 型が一切生成される前に設定する。
		aq::physics::PhysicsWorld::InstallAllocatorHook();

		if (!InitializeWindow(initializeParameter)) {
			aq::StartupLog("  [engine] InitializeWindow FAILED");
			return false;
		}
		aq::StartupLog("  [engine] window ok");
		if (!InitializeGraphicsAPI(initializeParameter)) {
			aq::StartupLog("  [engine] InitializeGraphicsAPI FAILED");
			return false;
		}
		aq::StartupLog("  [engine] graphics API ok");
		// ThreadPool のワーカ数はリソース予算で決める。
		// Win32 は 0(論理コア数)、Xbox(UWP)は 4コア占有+2コア共有に合わせて 6 固定。
		aq::util::ThreadPool::Initialize(aq::platform::GetResourceBudget().threadPoolWorkerCount);

		// サウンド: プラットフォームで選択したバックエンドを注入して初期化する（§10）。
		aq::sound::SoundEngine::Create<aq::sound::DefaultSoundBackend>();
		if (!aq::sound::SoundEngine::Get().Initialize()) {
			aq::StartupLog("  [engine] SoundEngine::Initialize FAILED");
			return false;
		}
		aq::StartupLog("  [engine] sound ok");

		// データ駆動オーディオ層（イベント/Bank）。SoundEngine の上に載る。
		aq::audio::AudioDirector::Create();
		aq::audio::AudioDirector::Get().Initialize();
		aq::StartupLog("  [engine] audio director ok");

		if (!application_->Initialize(renderContext_)) {
			aq::StartupLog("  [engine] application_->Initialize FAILED");
			return false;
		}
		aq::StartupLog("  [engine] application ok");
		application_->Register();

		gameTimer_.Initialize();

		return true;
	}


	void Engine::Finalize()
	{
		if (application_) {
			application_->Finalize();
			delete application_;
			application_ = nullptr;
		}

		// オーディオ層は SoundEngine より先に破棄する（SoundStream が SoundEngine を参照）。
		aq::audio::AudioDirector::Get().Finalize();
		aq::audio::AudioDirector::Release();

		aq::sound::SoundEngine::Get().Finalize();
		aq::sound::SoundEngine::Release();

		aq::graphics::GraphicsDevice::Get().Finalize();
		aq::graphics::GraphicsDevice::Release();

		aq::util::ThreadPool::Finalize();
		aq::memory::MemoryManager::Finalize();
	}


	const char* Engine::GetContentRoot() const
	{
		return platform_ ? platform_->GetContentRoot() : nullptr;
	}


	void Engine::RunGame()
	{
		// メッセージ/イベントのポンプはプラットフォーム層に委譲する。
		// PumpEvents() が終了要求で false を返すまで Update を回す。
		while (platform_->PumpEvents())
		{
			Update();
		}
	}


	bool Engine::InitializeWindow(const InitializeParameter& initializeParameter)
	{
		EngineAssert(initializeParameter.screenHeight);
		EngineAssert(initializeParameter.screenWidth);

		screenHeight_ = initializeParameter.screenHeight;
		screenWidth_  = initializeParameter.screenWidth;

		aq::platform::WindowDesc desc;
		desc.width  = initializeParameter.screenWidth;
		desc.height = initializeParameter.screenHeight;
		return platform_->CreateMainWindow(desc, window_);
	}


	bool Engine::InitializeGraphicsAPI(const InitializeParameter& initializeParameter)
	{
		renderWidth_  = initializeParameter.renderWidth;
		renderHeight_ = initializeParameter.renderHeight;

		// 選択された API の実装を注入 (将来 Vulkan に替える場合は define を変えてここを追加する)
#ifdef ENGINE_GRAPHICS_D3D11
		aq::graphics::GraphicsDevice::Create<aq::graphics::D3D11GraphicsDeviceImpl>();
#elif defined(ENGINE_GRAPHICS_D3D12)
		aq::graphics::GraphicsDevice::Create<aq::graphics::D3D12GraphicsDeviceImpl>();
#elif defined(ENGINE_GRAPHICS_VULKAN)
		aq::graphics::GraphicsDevice::Create<aq::graphics::VulkanGraphicsDeviceImpl>();
#endif

		if (!aq::graphics::GraphicsDevice::Get().Initialize(window_, renderWidth_, renderHeight_)) {
			return false;
		}

		aq::graphics::GraphicsDevice::Get().SetupRenderContext(renderContext_);
		aq::graphics::GraphicsDevice::Get().SetupDefaultRenderState(renderContext_);

		renderContext_.OMSetRenderTargets(
			1,
			&aq::graphics::GraphicsDevice::Get().GetMainRenderTarget(0)
		);
		renderContext_.RSSetViewport(
			0.0f, 0.0f,
			static_cast<float>(renderWidth_),
			static_cast<float>(renderHeight_)
		);

		return true;
	}


	void Engine::Update()
	{
		gameTimer_.Tick();
#ifdef AQ_RENDER_PIPELINED
		// 非同期: フレーム N とフレーム N+1 で別のメイン RT を使う。
		// これによりレンダースレッドがフレーム N（旧 RT）を実行している間に、
		// メインスレッドがフレーム N+1（新 RT）を構築でき、データ競合なく重複できる。
		// コマンドは記録時にハンドル index を焼き込むため、トグル後の構築でも整合する。
		ToggleMainRenderTarget();
#endif
		application_->Update();
		// サウンド: 終了ボイスの回収・バックエンドのポンプ（§2.1）。
		aq::sound::SoundEngine::Get().Update(gameTimer_.GetDeltaTime());
		// オーディオ層: イベントインスタンスの回収・クールダウン更新。
		aq::audio::AudioDirector::Get().Update(gameTimer_.GetDeltaTime());
		// FlushRender() はレンダースレッドがコマンドリストの実行・RT コピー・Present を
		// 完了するまで待機する。描画に関わるすべての D3D11 コンテキスト呼び出しは
		// レンダースレッド側に集約され、メインスレッドは Submit() 以降コンテキストに触れない。
		application_->FlushRender();
		aq::memory::MemoryManager::Get().ResetStackAllocator();
	}
}
