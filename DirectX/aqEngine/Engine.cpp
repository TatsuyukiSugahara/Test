#include "aq.h"
#include "Engine.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdlib>
#include "Resource/AssetPath.h"
#include "Core/IAppModule.h"
#include "Core/AppHost.h"
#include "Platform/IPlatform.h"
#include "Platform/PlatformBudget.h"
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
#elif defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalGraphicsDeviceImpl.h"
#endif


namespace aq
{
	Engine* Engine::instance_ = nullptr;


	void ShutdownMemory()
	{
		aq::memory::MemoryManager::Finalize();
	}


	Engine::Engine()
		: platform_(nullptr)
		, window_()
		, renderContext_()
		, currentMainRenderTarget_(0)
		, screenWidth_(0)
		, screenHeight_(0)
		, renderWidth_(0)
		, renderHeight_(0)
		, appHost_(nullptr)
	{
	}


	Engine::~Engine()
	{
	}


	bool Engine::IsEmbedded() const
	{
#if defined(AQ_PLATFORM_WIN32)
		return launchOptions_.parentWindow != 0;
#else
		return false;
#endif // AQ_PLATFORM_WIN32
	}


	bool Engine::HasInputFocus() const
	{
		return platform_ ? platform_->HasInputFocus() : true;
	}


	aq::graphics::NativeWindowHandle Engine::GetInputCooperativeWindow() const
	{
		// プラットフォームが専用のウィンドウを持たない(nullptr を返す)ときはメインウィンドウを使う
		aq::graphics::NativeWindowHandle window;
		if (platform_) {
			window = platform_->GetInputCooperativeWindow();
		}
		return window.handle ? window : window_;
	}


	bool Engine::Initialize(const InitializeParameter& initializeParameter)
	{
		platform_ = initializeParameter.platform;
		EngineAssert(platform_);

		launchOptions_ = initializeParameter.launch;
		aq::StartupMarkf("  [engine] launch: app=%s mode=%s editor-port=%u parent-hwnd=0x%llx",
			launchOptions_.appName.empty() ? "(default)" : launchOptions_.appName.c_str(),
			(launchOptions_.mode == LaunchMode::Edit) ? "edit" : "play",
			static_cast<uint32_t>(launchOptions_.editorPort),
			static_cast<unsigned long long>(launchOptions_.parentWindow));
		if (launchOptions_.mode == LaunchMode::Edit) {
			aq::StartupMarkf("  [engine] -mode=edit is not implemented yet; running as play");
		}
#if !defined(AQ_PLATFORM_WIN32)
		if (launchOptions_.parentWindow != 0) {
			aq::StartupMarkf("  [engine] -parent-hwnd is supported only on Win32; ignored");
		}
#endif // !AQ_PLATFORM_WIN32

#if defined(AQ_PLATFORM_WIN32)
		// 親ウィンドウの検証はモジュール生成より前に行う。ここで失敗すれば Finalize は何も触らずに戻れる
		// (ウィンドウ生成の段階で失敗すると、未生成のサブシステムを Finalize が触ってしまう)。
		if (IsEmbedded()) {
			const HWND parent = reinterpret_cast<HWND>(launchOptions_.parentWindow);
			RECT rc = {};
			if (!::IsWindow(parent) || !::GetClientRect(parent, &rc)
			 || rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) {
				aq::StartupMarkf("  [engine] invalid -parent-hwnd 0x%llx FAILED",
					static_cast<unsigned long long>(launchOptions_.parentWindow));
				return false;
			}
		}
#endif // AQ_PLATFORM_WIN32

		// 起動引数 -app= の名前(省略時は登録順の先頭)でモジュールを生成し、土台(AppHost)に所有させる。
		// 見つからなければ別のアプリで黙って動かさず、初期化失敗にする(エディタからの起動ミスに気付けるように)。
		// 旧 CreateApplication と同じく、メモリマネージャ初期化より前の生成になる。
		EngineAssertMsg(appHost_ == nullptr, "Engine::Initialize: application already created");
		std::unique_ptr<IAppModule> module = appModuleRegistry_.Create(launchOptions_.appName);
		if (!module) {
			std::string names;
			for (const std::string& name : appModuleRegistry_.GetNames()) {
				if (!names.empty()) {
					names += ", ";
				}
				names += name;
			}
			aq::StartupMarkf("  [engine] app module not found: %s (registered: %s) FAILED",
				launchOptions_.appName.empty() ? "(default)" : launchOptions_.appName.c_str(),
				names.empty() ? "none" : names.c_str());
			return false;
		}
		appHost_          = new AppHost(std::move(module));
		subsystemsStarted_ = true;

		// メモリマネージャを最初に初期化することで、ウィンドウ・グラフィクス初期化中の
		// new/delete もエンジンアロケータ管理下に置く。
		aq::memory::MemoryManager::Initialize(initializeParameter.memoryConfig);

		// "Assets/..." をどのフォルダへ組むかを決める。**アセット解決より前**に
		// 済ませる必要がある(解決はワーカースレッドから並列に走るので、走り出した後に
		// 変えるとデータ競合になる)。
		aq::res::SetGameRootName(initializeParameter.gameRootName);

		// Bullet allocator hook は MemoryManager 直後、かつ Bullet 型が一切生成される前に設定する。
		aq::physics::PhysicsWorld::InstallAllocatorHook();

		// COM(MTA) をメインスレッドでプロセス寿命ぶん保持する。XAudio2 / WIC(DirectXTex) / Media Foundation が
		// COM を要求し、MTA が 1 つでも存在すれば未初期化スレッド(ThreadPool ワーカ)も暗黙に MTA 参加扱いになる。
		// 従来は XAudio2 バックエンドのメインスレッド CoInitializeEx がこれを兼ねていたが、サウンド初期化を
		// 別スレッドへ移したため明示的にここで行う。COM は Windows 系プラットフォーム専用。
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
		comInitialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
#endif // AQ_PLATFORM_WINDOWS_FAMILY

		// サウンド初期化(XAudio2Create + マスタリングボイス)は実測 0.25〜1.0 秒かかり、その大半がデバイス待ちで
		// CPU を使わない。ウィンドウ/グラフィックス/シェーダ初期化と並列に別スレッドで走らせ、利用側は
		// EnsureSoundInitialized() で合流する(SoundEngine::Create 自体はここで済ませるので IsAvailable() は true)。
		aq::sound::SoundEngine::Create<aq::sound::DefaultSoundBackend>();
		soundInitFuture_ = std::async(std::launch::async, [] {
			return aq::sound::SoundEngine::Get().Initialize();
		});
		aq::StartupLog("  [engine] sound init started (async)");

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
		aq::StartupLog("  [engine] threadpool ok");

		// データ駆動オーディオ層（イベント/Bank）。SoundEngine の上に載る(Initialize は SoundEngine を触らない)。
		aq::audio::AudioDirector::Create();
		aq::audio::AudioDirector::Get().Initialize();
		aq::StartupLog("  [engine] audio director ok");

		// application 初期化中にサウンドが要るところ(BGM 開始など)は EnsureSoundInitialized() で合流する。
		if (!appHost_->Initialize(renderContext_)) {
			aq::StartupLog("  [engine] application Initialize FAILED");
			return false;
		}
		aq::StartupLog("  [engine] application ok");

		// application 側が合流していなくてもここで必ず待つ。以降 SoundEngine は初期化済みとして扱える。
		if (!EnsureSoundInitialized()) {
			return false;
		}
		if (!appHost_->Register()) {
			aq::StartupLog("  [engine] application Register FAILED");
			return false;
		}

		gameTimer_.Initialize();
		gameTimer_.SetFixedFPS(static_cast<float>(launchOptions_.fixedFps));

		return true;
	}


	void Engine::Finalize()
	{
		// モジュールの生成で失敗したときは、まだ何も作っていない(下の Get() は生成前だと落ちる)。
		if (!subsystemsStarted_) {
			return;
		}

		if (appHost_) {
			appHost_->Finalize();
			delete appHost_;
			appHost_ = nullptr;
		}

		// オーディオ層は SoundEngine より先に破棄する（SoundStream が SoundEngine を参照）。
		aq::audio::AudioDirector::Get().Finalize();
		aq::audio::AudioDirector::Release();

		// 初期化途中で失敗した場合など、サウンド初期化スレッドが未合流なら破棄前に待つ。
		if (soundInitFuture_.valid()) {
			soundInitFuture_.wait();
		}
		if (aq::sound::SoundEngine::IsAvailable()) {
			aq::sound::SoundEngine::Get().Finalize();
			aq::sound::SoundEngine::Release();
		}

		aq::graphics::GraphicsDevice::Get().Finalize();
		aq::graphics::GraphicsDevice::Release();

		aq::util::ThreadPool::Finalize();

		// COM はワーカ(WIC 等)が全て止まった後に解放する。
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
		if (comInitialized_) {
			CoUninitialize();
			comInitialized_ = false;
		}
#endif // AQ_PLATFORM_WINDOWS_FAMILY

		// MemoryManager はここでは畳まない。Engine 本体とプラットフォームがまだ生きているうちに
		// リーク報告を出すと、それらが全部「リーク」として並んでしまう。
		// 破棄は Engine::Release() の後、エントリ側の ShutdownMemory() が行う。
	}


	void Engine::WaitForRenderIdle()
	{
		if (appHost_) {
			appHost_->WaitForRenderIdle();
		}
	}


	bool Engine::EnsureSoundInitialized()
	{
		if (soundInitFuture_.valid()) {
			soundInitialized_ = soundInitFuture_.get();   // get() で future は無効化され 2 回目以降はここを通らない
			aq::StartupLog(soundInitialized_ ? "  [engine] sound ok (joined)"
			                                 : "  [engine] SoundEngine::Initialize FAILED");
		}
		return soundInitialized_;
	}


	const char* Engine::GetContentRoot() const
	{
		return platform_ ? platform_->GetContentRoot() : nullptr;
	}


	const char* Engine::GetUserDataDirectory() const
	{
		return platform_ ? platform_->GetUserDataDirectory() : nullptr;
	}


	void Engine::FrameStep()
	{
		// 背面に回ったらサウンドも止める。描画と違ってフレームを飛ばすだけでは鳴り続ける。
		SyncSoundActivity();

		// リサイズの致命失敗後は、デバイスが壊れている可能性があるので Update・描画・リサイズ・
		// サーフェス再作成をせず、終了(Finalize)を待つ。
		if (fatalStopped_) {
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
			return;
		}

		// 描画対象が差し替わったらサーフェスを作り直してから描く。
		// Android ではバックグラウンド復帰・回転のたびにここへ来る。
		if (!EnsureSurfaceUpToDate()) {
			return;
		}

		// 提示先が無い間(Android のバックグラウンド等)はフレームごと飛ばす。
		// PumpEvents 側がイベント待ちでブロックするので、ここは空転しない。
		if (!platform_->IsRenderable()) {
			return;
		}

		SyncScreenSize();

		// 描画解像度のリサイズ。通常(1 回で成功)はここで済ませて同じフレームの Update へ進む。
		// 失敗経路でだけ false が返り、Update を飛ばす(サウンドの供給だけは続ける)。
		if (!HandleRenderResize()) {
			UpdateSoundOnly();
			// Present もイベント待ちも無いので、待たないと再試行の間隔(200 ms)のあいだ CPU を使い切る。
			// サウンドの供給(バッファは最大約 680 ms)に影響しない短さで譲る。
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			return;
		}

		// 撮影用: 指定フレームまで更新したら、以降は更新も描画も止めて最後の画面を残す(-freeze-frame)。
		if (launchOptions_.freezeFrame != 0 && updatedFrames_ >= launchOptions_.freezeFrame)
		{
			if (!freezeLogged_) {
				aq::StartupMarkf("[engine] freeze: %u フレームで更新を止めた(-freeze-frame)", updatedFrames_);
				freezeLogged_ = true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
			return;
		}
		++updatedFrames_;

		Update();
	}


	void Engine::RequestExit(int exitCode)
	{
		if (!exitCodeSet_)
		{
			exitCode_    = exitCode;
			exitCodeSet_ = true;
		}
		if (platform_) {
			platform_->RequestExit();
		}
	}


	void Engine::RunGame()
	{
		// メッセージ/イベントのポンプもフレーム駆動もプラットフォーム層に委譲する。
		// 既定実装は「PumpEvents() が終了要求で false を返すまで FrameStep を回す」で、
		// Win32 / UWP / Mac / Android は従来どおりここでループする。iOS だけは
		// UIApplicationMain が run loop を持つため、CADisplayLink を張って即戻る
		// (設計書/iOS移植設計.md §3.3)。
		platform_->RunFrameLoop([this] { FrameStep(); });
	}


	void Engine::SyncSoundActivity()
	{
		// Android では「描画対象を持っているか」がそのまま「前面にいるか」になる。
		// 通知シェードを下ろしただけ(フォーカス喪失)では窓は生きているので止めない。
		const bool active = platform_->IsRenderable();
		if (active == soundActive_) {
			return;
		}
		soundActive_ = active;

		if (!aq::sound::SoundEngine::IsAvailable()) {
			return;
		}
		if (active) {
			aq::sound::SoundEngine::Get().OnResume();
		} else {
			aq::sound::SoundEngine::Get().OnSuspend();
		}
	}


	bool Engine::SyncScreenSize()
	{
		// 回転やリサイズで提示面の寸法が変わる。ImGui の DisplaySize や仮想パッドの
		// 当たり判定はスクリーン座標で持っているので、実際の面に追従させる。
		// レンダー解像度 (オフスクリーン RT) は作り直さないため据え置き。
		// ここはフレームの外 (直列構成ならレンダースレッドは停止中) で、読む値も
		// レンダースレッドがフレーム境界でしか書き換えないもの。
		uint32_t width  = 0;
		uint32_t height = 0;
		if (!aq::graphics::GraphicsDevice::Get().GetSurfaceSize(width, height)) {
			return false;
		}
		if (width == 0 || height == 0) {
			return false;
		}
		screenWidth_  = width;
		screenHeight_ = height;
		return true;
	}


	namespace
	{
		/** 再試行の上限と間隔(設計書/エンジン課題解消設計.md P4 §5.2)。上限を超えたら致命エラー */
		constexpr uint32_t RESIZE_MAX_RETRIES = 5;
		constexpr std::chrono::milliseconds RESIZE_RETRY_INTERVAL{ 200 };
	}


	bool Engine::HandleRenderResize()
	{
		auto& device = aq::graphics::GraphicsDevice::Get();
		if (!appHost_ || !device.SupportsRenderResize()) {
			return true;   // 未対応のバックエンドは従来どおり固定寸法
		}

		// 保留中(失敗経路)は、寸法の検出をやり直さず、前回から間隔をあけて再試行する。
		if (resizePending_)
		{
			if (std::chrono::steady_clock::now() - lastResizeAttempt_ < RESIZE_RETRY_INTERVAL) {
				return false;
			}
			return AttemptRenderResize();
		}

		// 寸法は Platform が持つ値から取る(デバイスの内部状態はレンダースレッドが書き換え得るので読まない)。
		// 取れない Platform だけ従来の GetSurfaceSize にフォールバックする。
		uint32_t width  = 0;
		uint32_t height = 0;
		if (!platform_->GetClientPixelSize(width, height) && !device.GetSurfaceSize(width, height)) {
			return true;
		}
		if (width == 0 || height == 0) {
			return true;   // 最小化など。現状維持
		}

		if (width == lastRequestedW_ && height == lastRequestedH_) {
			candidateFrames_ = 0;
			return true;
		}

		// ドラッグ中の連続変更で毎フレーム作り直さないよう、同じ値が 2 フレーム続いたら確定する。
		if (width != candidateW_ || height != candidateH_) {
			candidateW_      = width;
			candidateH_      = height;
			candidateFrames_ = 1;
			return true;
		}
		if (++candidateFrames_ < 2) {
			return true;
		}
		candidateFrames_ = 0;

		// 手順 0: 対応可否の判定。デバイスを変える前に、全パスが対応しているかを確かめる
		// (デバイスだけ新寸法になり、未対応パスが旧寸法のまま取り残されるのを防ぐ入口)。
		lastRequestedW_ = width;   // 失敗・無効でも毎フレーム再要求しない(寸法がまた変わったときだけ再び来る)
		lastRequestedH_ = height;
		if (!appHost_->CanResizeRender())
		{
			if (!resizeDisabledLogged_) {
				aq::StartupMark("[engine] リサイズ無効: パイプラインに未対応のパスがある(固定寸法のまま続行)");
				resizeDisabledLogged_ = true;
			}
			return true;
		}

		resizePending_    = true;
		resizeDeviceDone_ = false;
		pendingReqW_      = width;
		pendingReqH_      = height;
		resizeRetries_    = 0;
		lastSoundOnlyTime_ = std::chrono::steady_clock::now();
		aq::StartupMarkf("[engine] リサイズ開始: %ux%u -> %ux%u", renderWidth_, renderHeight_, width, height);
		return AttemptRenderResize();
	}


	bool Engine::AttemptRenderResize()
	{
		auto& device = aq::graphics::GraphicsDevice::Get();
		lastResizeAttempt_ = std::chrono::steady_clock::now();

		auto fail = [this](const char* what)
		{
			++resizeRetries_;
			aq::StartupMarkf("[engine] リサイズ失敗(%s)。再試行 %u/%u", what, resizeRetries_, RESIZE_MAX_RETRIES);
			if (resizeRetries_ >= RESIZE_MAX_RETRIES)
			{
				// リサイズで確保できないのはデバイスロスト / メモリ枯渇に相当し、描画を続けても壊れた画面になる。
				// 終了コード 2 で終了を要求し、以後のフレームは何もしない(Finalize は通常どおり走る。設計書 エンジン課題解消設計 §9.2)。
				aq::StartupMark("[engine] FATAL: リサイズを諦める(再試行の上限)");
				fatalStopped_ = true;
				RequestExit(2);
			}
			return false;   // 保留のまま。このフレームは Update を飛ばす
		};

		// 手順 1: CPU・GPU 双方の完了待ち。以降はフレームの外(レンダースレッド停止中)。
		appHost_->WaitForRenderIdle();

		// 手順 2: デバイス(スワップチェーン + メイン RT / 深度)。確定寸法を受け取る。
		if (!resizeDeviceDone_)
		{
			uint32_t outW = 0;
			uint32_t outH = 0;
			if (!device.ResizeMainTargets(pendingReqW_, pendingReqH_, outW, outH) || outW == 0 || outH == 0) {
				return fail("デバイス");
			}
			pendingOutW_      = outW;
			pendingOutH_      = outH;
			resizeDeviceDone_ = true;
		}

		// 手順 3: 全パスの準備。寸法は要求値ではなくデバイスが確定した値で作る(メイン RT・提示面・全パスを一致させる)。
		if (!appHost_->PrepareRenderResize(pendingOutW_, pendingOutH_)) {
			return fail("パス");   // 失敗したパス自身と成功済みパスは PrepareResize が片付け済み
		}

		// 手順 4: 確定(掲示板を初期状態へ戻して全パスが登録し直す。旧 RT の解放)。
		appHost_->CommitRenderResize(pendingOutW_, pendingOutH_);

		// 手順 5: Engine の寸法を更新し、保留を解除する。
		renderWidth_  = pendingOutW_;
		renderHeight_ = pendingOutH_;

		// 提示面の寸法もこのフレームのうちに新しくする(設計書/エディタ全体設計.md 5.9 節 2.5)。
		// ゲーム内 UI の当たり判定は screenWidth_ を使うので、次のフレームまで旧い値を残さない。
		// GetSurfaceSize を持たないデバイス(D3D12)は、確定した提示面の寸法を入れる。
		if (!SyncScreenSize())
		{
			screenWidth_  = pendingOutW_;
			screenHeight_ = pendingOutH_;
		}
		resizePending_ = false;
		aq::StartupMarkf("[engine] リサイズ完了: %ux%u", renderWidth_, renderHeight_);

		// 再作成に掛かった時間が次フレームのデルタ時間に載らないようにする。
		gameTimer_.ResetDelta();
		return true;
	}


	void Engine::UpdateSoundOnly()
	{
		// Update を飛ばすとストリーミング音声の供給も止まり、既存バッファ(48 kHz で最大約 680 ms)を使い切ると音が切れる。
		// GameTimer は Tick されないので前回のデルタ時間を繰り返し使ってしまい、フェードが実時間より速く進む。
		// よって実経過時間を自前で測って渡す。
		const auto now = std::chrono::steady_clock::now();
		const float dt = std::min(std::chrono::duration<float>(now - lastSoundOnlyTime_).count(), 0.1f);
		lastSoundOnlyTime_ = now;

		aq::sound::SoundEngine::Get().Update(dt);
		aq::audio::AudioDirector::Get().Update(dt);
	}


	bool Engine::EnsureSurfaceUpToDate()
	{
		// 通知はラッチなので取りこぼさないよう毎回引く。実際に作り直せるまで
		// surfaceDirty_ は落とさない(1 回失敗したら次のループで再試行する)。
		aq::graphics::NativeWindowHandle newWindow;
		if (platform_->ConsumeSurfaceChanged(newWindow)) {
			window_       = newWindow;
			surfaceDirty_ = true;
		}
		if (!surfaceDirty_) {
			return true;
		}

		// 窓を取り上げられている間は作り直しても失敗するだけ。復帰時に改めて通知が来る。
		if (!platform_->IsRenderable()) {
			return false;
		}

		// 在フライトのコマンドが古いサーフェスの画像を参照したまま破棄されないよう、
		// CPU・GPU 双方の完了を待ってから作り直す。
		if (appHost_) {
			appHost_->WaitForRenderIdle();
		}

		if (!aq::graphics::GraphicsDevice::Get().RecreateSurface(window_)) {
			aq::StartupLog("  [engine] RecreateSurface FAILED");
			return false;
		}
		surfaceDirty_ = false;
		return true;
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

#if defined(AQ_PLATFORM_WIN32)
		// 埋め込み時は親のクライアント全面に子ウィンドウを作るので、スクリーンサイズも親に合わせる。
		// レンダー解像度は引数のまま(オフスクリーン RT から拡縮して提示する)。
		// 親のリサイズには追従しない(起動時の大きさで固定)。
		if (IsEmbedded()) {
			const HWND parent = reinterpret_cast<HWND>(launchOptions_.parentWindow);
			RECT rc = {};
			if (!::IsWindow(parent) || !::GetClientRect(parent, &rc)
			 || rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) {
				aq::StartupMarkf("  [engine] invalid -parent-hwnd 0x%llx FAILED",
					static_cast<unsigned long long>(launchOptions_.parentWindow));
				return false;
			}
			screenWidth_      = static_cast<uint32_t>(rc.right - rc.left);
			screenHeight_     = static_cast<uint32_t>(rc.bottom - rc.top);
			desc.width        = static_cast<int32_t>(screenWidth_);
			desc.height       = static_cast<int32_t>(screenHeight_);
			desc.parentWindow = parent;
		}
#endif // AQ_PLATFORM_WIN32

		return platform_->CreateMainWindow(desc, window_);
	}


	bool Engine::InitializeGraphicsAPI(const InitializeParameter& initializeParameter)
	{
		renderWidth_  = initializeParameter.renderWidth;
		renderHeight_ = initializeParameter.renderHeight;
		lastRequestedW_ = renderWidth_;    // リサイズの検出は、起動時の寸法を「要求済み」として始める
		lastRequestedH_ = renderHeight_;

		// 選択された API の実装を注入 (将来 Vulkan に替える場合は define を変えてここを追加する)
#ifdef ENGINE_GRAPHICS_D3D11
		aq::graphics::GraphicsDevice::Create<aq::graphics::D3D11GraphicsDeviceImpl>();
#elif defined(ENGINE_GRAPHICS_D3D12)
		aq::graphics::GraphicsDevice::Create<aq::graphics::D3D12GraphicsDeviceImpl>();
#elif defined(ENGINE_GRAPHICS_VULKAN)
		aq::graphics::GraphicsDevice::Create<aq::graphics::VulkanGraphicsDeviceImpl>();
#elif defined(ENGINE_GRAPHICS_METAL)
		aq::graphics::GraphicsDevice::Create<aq::graphics::MetalGraphicsDeviceImpl>();
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
		appHost_->Update();
		// サウンド: 終了ボイスの回収・バックエンドのポンプ（§2.1）。
		aq::sound::SoundEngine::Get().Update(gameTimer_.GetDeltaTime());
		// オーディオ層: イベントインスタンスの回収・クールダウン更新。
		aq::audio::AudioDirector::Get().Update(gameTimer_.GetDeltaTime());
		// FlushRender() はレンダースレッドがコマンドリストの実行・RT コピー・Present を
		// 完了するまで待機する。描画に関わるすべての D3D11 コンテキスト呼び出しは
		// レンダースレッド側に集約され、メインスレッドは Submit() 以降コンテキストに触れない。
		appHost_->FlushRender();
		aq::memory::MemoryManager::Get().ResetStackAllocator();
#if defined(AQ_ECS_CHUNK_STATS)
		// Chunk 統計(設計書/アロケーター改良設計.md §0.3)。フレーム番号を進め、
		// 300 フレームごとにスナップショットを溜める(出力は終了時に 1 回だけ)。
		// ここは ForEach / FlushCommands の外なので、収集のリードロックを安全に取れる。
		{
			static uint32_t statsFrame = 0;
			aq::ecs::AdvanceChunkStatsFrame();
			if (++statsFrame % 300 == 0) {
				aq::ecs::CaptureChunkStats("auto");
			}
		}
#endif
	}
}
