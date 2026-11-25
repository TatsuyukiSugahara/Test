#include "aq.h"
#include "AppHost.h"
#include "ECS/EntityContext.h"
#include "HID/Input.h"
#include "HID/TouchGesture.h"
#if defined(AQ_PLATFORM_ANDROID) || defined(AQ_PLATFORM_IOS)
#include "HID/VirtualPadBackend.h"   // 仮想パッドの当たり判定レイアウト(暫定の可視化用)
#endif
#include "UI/Input/UIInputSystem.h"
#include "UI/Rendering/UIBatchRenderer.h"
#include "Component/AnimationComponentSystem.h"
#include "Component/ParticleComponentSystem.h"
#include "ECS/SpawnSystem.h"
// エンジン既定のリソースバンク登録(RegisterEngineResourceBanks)で要る型とローダー。
#include "Resource/ParticleSystemData.h"
#include "Resource/ParticleLoader.h"
#include "UI/Font/FontResource.h"
#include "Sound/SoundClip.h"
#include "Util/Profiler.h"
#include "Platform/IPlatform.h"
#include "Rendering/Occlusion/HiZRenderer.h"
#include "Rendering/Occlusion/GpuClusterCuller.h"
#include "Rendering/Occlusion/ClusterCull.h"   // SetClusterCullEnabled
#include "Rendering/Pipeline/Passes/SSRPass.h"       // SSR デバッグパネルの配線に使う
#include "Rendering/Pipeline/Passes/SSAOPass.h"      // SSAO デバッグパネルの配線に使う
#include "Rendering/Pipeline/Passes/DoFPass.h"       // DoF デバッグパネルの配線に使う
#include "Rendering/SSR/SSRRenderer.h"
#include "Rendering/Pipeline/Passes/HiZPass.h"      // Hi-Z オクリュージョンの配線に使う
#include "Rendering/Pipeline/Passes/ShadowPass.h"   // デバッグパネルの配線に使う
#include "Rendering/Pipeline/Passes/GBufferPass.h"  // 同上
// SetupStandardRenderers が生成する具象レンダラ群。
#include "Rendering/Shadow/HardShadowRenderer.h"
#include "Rendering/Deferred/DeferredRenderer.h"
#include "Graphics/InstancedStaticMesh.h"      // Finalize での名前レジストリ解放
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Occlusion/Debug/CullingDebugPanel.h"
#include "Rendering/PostProcess/Debug/PostProcessDebugPanel.h"
#endif
#ifdef AQ_IMGUI
#include <imgui/imgui.h>
#include "Rendering/ImGuiRenderCommand.h"
#endif
#if AQ_EDITOR
#ifndef AQ_IMGUI
#error "AQ_EDITOR=1 の構成では AQ_IMGUI も定義すること(エディタは ImGui で描く)"
#endif
#include "Editor/IEditorLayer.h"
#endif
#include "ECS/ComponentRegistry.h"   // JSON シリアライズ用。常時コンパイル（AQ_DEBUG_IMGUI 非依存）。
#include "Level/LevelComponentRegistry.h"
#include "Level/LevelStreamSystem.h"
#include "Level/LevelManager.h"
#include "Link/LinkService.h"
#include "Link/LinkCommands.h"
#ifdef AQ_DEBUG_IMGUI
#include "DebugUI.h"
#include "Ocean/Debug/OceanDebugPanel.h"
#include "Rendering/Debug/RenderingDebugPanel.h"
#include "ECS/SceneHierarchySystem.h"
#include "UI/Debug/UIEditorDebugPanel.h"
#endif


namespace aq
{
	namespace
	{
#ifdef AQ_IMGUI
		/**
		 * ゲーム用 ImGui コンテキストにプラットフォームの入力バックエンド(win32 など)を繋ぐか。
		 * エディタ構成ではエディタ用コンテキストにだけ繋ぎ、ゲーム用には繋がない。
		 * 繋がないときは、NewFrame 前の DisplaySize / DeltaTime を AppHost が自前で入れる。
		 */
		static constexpr bool GAME_IMGUI_USES_PLATFORM_BACKEND = (AQ_EDITOR == 0);
#endif


		/**
		 * エンジンが所有するリソース型のバンクとローダーを登録する。
		 *
		 * **ゲーム側ではなくここで登録する。** 9 型すべて `aq::` 所有で、ゲームが
		 * 登録する理由がない(設計書/使いやすさ改善設計.md P2-A)。
		 *
		 * **呼ぶ位置が重要。** `ResourceManager::Initialize()` の直後、つまり
		 * `OnInitialize()` より前に済ませる。以前はゲームの `OnRegister()`
		 * (= `OnInitialize()` の後)で登録していたため、`OnInitialize` で
		 * `Load` するとバンクが無く `EngineAssert` で落ちた。その回避として
		 * SkyRenderer の生成や UI の先読みが後段へ逃がされていた。
		 *
		 * `ComponentRegistry::RegisterCoreComponents()` と同じ流儀
		 * (エンジンの初期化中にエンジン既定を登録する)。
		 */
		void RegisterEngineResourceBanks()
		{
			using namespace aq::res;

			ResourceManager::RegisterBank<GPUResource,          TResourceBank<GPUResource>>();
			ResourceManager::RegisterBank<MeshResource,         TResourceBank<MeshResource>>();
			ResourceManager::RegisterBank<PMDResource,          TResourceBank<PMDResource>>();
			ResourceManager::RegisterBank<ShaderResource,       TResourceBank<ShaderResource>>();
			ResourceManager::RegisterBank<SkeletalMeshResource, TResourceBank<SkeletalMeshResource>>();
			ResourceManager::RegisterBank<AnimationResource,    TResourceBank<AnimationResource>>();
			ResourceManager::RegisterBank<ui::FontResource,     TResourceBank<ui::FontResource>>();
			ResourceManager::RegisterBank<sound::SoundClip,     TResourceBank<sound::SoundClip>>();
			ResourceManager::RegisterBank<ParticleSystemData,   TResourceBank<ParticleSystemData>>();

			ResourceManager::Reflection<GPUResource,          TextureLoader>();
			ResourceManager::Reflection<MeshResource,         MeshLoader>();
			ResourceManager::Reflection<PMDResource,          PMDLoader>();
			ResourceManager::Reflection<ShaderResource,       ShaderLoader>();
			ResourceManager::Reflection<SkeletalMeshResource, SkeletalMeshLoader>();
			ResourceManager::Reflection<AnimationResource,    AnimationLoader>();
			ResourceManager::Reflection<ui::FontResource,     ui::FontLoader>();
			ResourceManager::Reflection<sound::SoundClip,     sound::SoundClipLoader>();
			ResourceManager::Reflection<ParticleSystemData,   ParticleLoader>();
		}
	}


	AppHost* AppHost::instance_ = nullptr;


	AppHost::AppHost(std::unique_ptr<IAppModule> module)
		: module_(std::move(module))
	{
		EngineAssertMsg(module_ != nullptr, "AppHost requires an IAppModule");
		EngineAssert(instance_ == nullptr);
		instance_ = this;
	}


	AppHost::~AppHost()
	{
		// 旧来の継承構成ではゲーム側(派生)の破棄が土台のメンバより先に走っていた。
		// その順序を保つため、モジュールを明示的に先に破棄する。
		module_.reset();
		instance_ = nullptr;
	}


	rendering::PipelineBuilder AppHost::BuildStandardPipeline(const RendererPreset& preset)
	{
		// worldPos (GBuffer2) → MotionBlurPass の配線や Shadow/Sky の生成失敗時のフォールバックは
		// 各パスの Setup() / Build() 側の責務になった(設計書/レンダーパイプライン設計.md §2)。
		// preset.pipeline の既定は Standard なので、何もしなければ従来と同じ列になる。
		return rendering::PipelinePresets::ForCurrentPlatform(preset,
			[](aq::rendering::RenderCommandList& list)
			{
				aq::ui::UIContext::Get().GetBatchRenderer().BuildCommandList(list);
			});
	}


	bool AppHost::SetupStandardRenderers(const RendererPreset& preset)
	{
		auto builder = BuildStandardPipeline(preset);

		const uint32_t renderW = Engine::Get().GetRenderWidth();
		const uint32_t renderH = Engine::Get().GetRenderHeight();

		auto pipeline = builder.Build(renderW, renderH);
		if (!pipeline)
		{
			// 理由は Build() が [pipeline] ログへ既に出している。
			// EngineAssertMsg は Release では no-op なので、ログと戻り値で必ず失敗を伝える。
			aq::StartupMark("  [app] ERROR: standard pipeline Build failed (see [pipeline] log)");
			EngineAssertMsg(false, "PipelinePresets::ForCurrentPlatform: PipelineBuilder::Build failed (see [pipeline] log)");
			return false;
		}

		renderer_.SetPipeline(std::move(pipeline),
		                      Engine::Get().GetMainRenderTargetHandle(),
		                      static_cast<float>(renderW),
		                      static_cast<float>(renderH));

		aq::StartupMark("  [app] standard renderers ok (shadow/deferred/postprocess/sky)");
		return true;
	}


	bool AppHost::SetRenderPipeline(std::unique_ptr<rendering::RenderPipeline> pipeline)
	{
		// Build 失敗の nullptr がそのまま来る。設定すると Renderer が全描画を黙って捨てる
		// (Renderer::Render の `if (!pipeline_) return;`)ので、ここで失敗として返す。
		if (!pipeline)
		{
			aq::StartupMark("  [app] ERROR: SetRenderPipeline got null (PipelineBuilder::Build failed, see [pipeline] log)");
			EngineAssertMsg(false, "AppHost::SetRenderPipeline: pipeline is null");
			return false;
		}

		const uint32_t renderW = Engine::Get().GetRenderWidth();
		const uint32_t renderH = Engine::Get().GetRenderHeight();

		renderer_.SetPipeline(std::move(pipeline),
		                      Engine::Get().GetMainRenderTargetHandle(),
		                      static_cast<float>(renderW),
		                      static_cast<float>(renderH));
		return true;
	}


	bool AppHost::CanResizeRender()
	{
		rendering::RenderPipeline* pipeline = renderer_.GetPipeline();
		return pipeline != nullptr && pipeline->CanResize();
	}


	bool AppHost::PrepareRenderResize(const uint32_t width, const uint32_t height)
	{
		rendering::RenderPipeline* pipeline = renderer_.GetPipeline();
		return pipeline != nullptr && pipeline->PrepareResize(width, height);
	}


	void AppHost::AbortRenderResize()
	{
		if (rendering::RenderPipeline* pipeline = renderer_.GetPipeline()) {
			pipeline->AbortResize();
		}
	}


	void AppHost::CommitRenderResize(const uint32_t width, const uint32_t height)
	{
		if (rendering::RenderPipeline* pipeline = renderer_.GetPipeline()) {
			pipeline->CommitResize();
		}
		renderer_.SetMainViewport(static_cast<float>(width), static_cast<float>(height));

		// メインカメラのアスペクト比(ビューポート寸法)。ゲームが初期化時に 1 回だけ設定した値を、新寸法へ追従させる。
		if (auto* mainCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main)) {
			mainCamera->SetViewportSize(static_cast<float>(width), static_cast<float>(height));
		}
	}


	bool AppHost::Initialize(aq::graphics::RenderContext& renderContext)
	{
#ifdef AQ_PROFILE_ENABLED
		aq::profile::Profiler::Get().SetThreadName("Main");
#endif
		renderThread_.Initialize(&renderContext);
		renderThreadReady_ = true;

		aq::res::ResourceManager::Initialize();
		// バンクは OnInitialize より前に揃える(上の関数コメントの「呼ぶ位置が重要」)。
		RegisterEngineResourceBanks();
		aq::ecs::EntityContext::Initialize();
		aq::hid::InputManager::Initialize();
		// InputManager::Setup() は P1 の入力 Bridge 化で HRESULT → bool になった
		if (!aq::hid::InputManager::Get().Setup())
		{
			EngineAssertMsg(false, "InputManager::Setup failed: keyboard/mouse backend initialization failed");
			return false;
		}
		aq::StartupMark("  [app] input ok");
		aq::ui::UIContext::Initialize();
		aq::StartupMark("  [app] UIContext ok (UI shaders x4 compiled)");
		aq::CameraManager::Initialize();
		aq::graphics::LightManager::Initialize();
#ifdef AQ_DEBUG_IMGUI
		aq::DebugUI::Initialize();
#endif
		// レジストリは JSON シリアライズ/デシリアライズに使うため、ビルド構成を問わず登録する。
		aq::ecs::ComponentRegistry::RegisterCoreComponents();
		aq::level::RegisterLevelComponents();   // Level 層のコンポーネント（LevelStream 等）を登録

#ifdef AQ_IMGUI
		{
			gameImGuiContext_ = ImGui::CreateContext();
#if AQ_EDITOR
			// エディタ用コンテキスト。フォントアトラスはゲーム用と共有する(この後で読むフォントが両方に効く)。
			// CreateContext が current を変えるかに依存しないよう、明示的にゲーム用へ戻す。
			{
				ImFontAtlas* sharedAtlas = ImGui::GetIO().Fonts;
				editorImGuiContext_ = ImGui::CreateContext(sharedAtlas);
				ImGui::SetCurrentContext(gameImGuiContext_);
			}
#endif

#if defined(AQ_PLATFORM_IOS)
			// iOS はアプリバンドルが read-only で、カレントディレクトリも当てにできない
			// (設計書/iOS移植設計.md §3.6 / §7.2)。ImGui 既定の "imgui.ini" は CWD 相対なので
			// 黙って保存に失敗し、デバッグ UI の配置が毎回リセットされる形で表面化する。
			// 書き込める GetUserDataDirectory() 配下へ向ける。
			//
			// io.IniFilename は **const char* を保持するだけでコピーしない**。文字列の実体は
			// プロセス寿命で生かす必要があるため関数ローカルの static に置く
			// (ローカルの std::string にすると即ダングリングポインタになる)。
			// std::string ではなく素の配列にしてあるのは、ShutdownMemory(MemoryManager の
			// リーク報告)より後まで生き残るぶんを未解放として数えさせないため。
			{
				static constexpr size_t IMGUI_INI_PATH_BUFFER_COUNT = 512;
				static char s_imguiIniPath[IMGUI_INI_PATH_BUFFER_COUNT] = {};

				// 書き込める場所が分からない/長すぎて収まらないなら、毎回保存に失敗し
				// 続けるより保存を切る(nullptr = ini を読み書きしない)。
				const char* iniFilename   = nullptr;
				const char* userDirectory = Engine::Get().GetUserDataDirectory();
				if (userDirectory != nullptr)
				{
					// GetUserDataDirectory は末尾セパレータ付きの契約(IPlatform.h)。
					const int written = snprintf(s_imguiIniPath, sizeof(s_imguiIniPath), "%simgui.ini", userDirectory);
					if (written > 0 && static_cast<size_t>(written) < sizeof(s_imguiIniPath)) {
						iniFilename = s_imguiIniPath;
					}
				}
				ImGui::GetIO().IniFilename = iniFilename;
			}
#endif // AQ_PLATFORM_IOS

				// ImGui 用フォントをシステムのフォントフォルダから読み込む。
				// 日本語グリフ(かな/CJK 統合漢字 約 21,000 字)は起動時のアトラス生成に約 0.26 秒かかる一方、
				// 使用箇所は Debug パネルの一部ラベルのみだったため ASCII(+矢印/図形記号)に制限した。
				// 日本語ラベルは "?" で表示される。必要なら kCustomRanges に範囲を足す。
				{
					ImGuiIO& io = ImGui::GetIO();
					bool fontLoaded = false;

#if defined(AQ_PLATFORM_WIN32) || defined(AQ_PLATFORM_MAC)
					// ASCII/Latin-1 + Arrows (U+2190-21FF) + Geometric Shapes (U+25A0-25FF) のみ。
					// 日本語範囲(U+3000-30FF / U+31F0-31FF / U+FF00-FFEF / U+4E00-9FAF)は起動短縮のため外した。
					// 範囲表とロード手順は Windows / Mac で共用し、フォントの探索先だけを分ける。
					static const ImWchar kCustomRanges[] = {
						0x0020, 0x00FF, // Basic Latin + Latin-1
						0x2190, 0x21FF, // Arrows (→←↑↓↖↗↘↙↕ 等)
						0x25A0, 0x25FF, // Geometric Shapes (●▶◀▲▼ 等)
						0,
					};

					// 読めたら true。TTC はコレクション内の先頭フォントを使う。
					auto tryLoadFont = [&io](const char* path) -> bool
					{
						FILE* f = fopen(path, "rb");
						if (!f) return false;
						fclose(f);

						ImFontConfig cfg;
						cfg.FontNo = 0;
						return io.Fonts->AddFontFromFileTTF(path, 15.0f, &cfg, kCustomRanges) != nullptr;
					};
#endif

#if defined(AQ_PLATFORM_WIN32)
					// Windows フォントフォルダ(GetWindowsDirectoryA で動的解決)。
					char winDir[MAX_PATH] = {};
					if (GetWindowsDirectoryA(winDir, MAX_PATH) == 0)
						snprintf(winDir, sizeof(winDir), "%s", "C:\\Windows");

					static const char* kJpFontNames[] = {
						"meiryo.ttc",    // Meiryo (Vista+、推奨)
						"YuGothR.ttc",   // Yu Gothic Regular (Win8.1+)
						"msgothic.ttc",  // MS Gothic (XP+、フォールバック)
					};
					for (const char* name : kJpFontNames)
					{
						char path[MAX_PATH];
						snprintf(path, sizeof(path), "%s\\Fonts\\%s", winDir, name);
						if (tryLoadFont(path)) { fontLoaded = true; break; }
					}
#elif defined(AQ_PLATFORM_MAC)
					// macOS のシステムフォント。SFNS が標準で、無い環境向けに Helvetica を残す。
					static const char* kMacFontPaths[] = {
						"/System/Library/Fonts/SFNS.ttf",       // San Francisco (macOS 11+)
						"/System/Library/Fonts/Helvetica.ttc",  // フォールバック
					};
					for (const char* path : kMacFontPaths)
					{
						if (tryLoadFont(path)) { fontLoaded = true; break; }
					}
#endif
					// UWP はフォントファイルを直接読めないため、常に既定フォントになる。
					if (!fontLoaded)
						io.Fonts->AddFontDefault();
				}

#if defined(AQ_PLATFORM_ANDROID) || defined(AQ_PLATFORM_IOS)
				// 指で触れる大きさにする。2400x1080 の端末ではメニューバーの高さが 10px 程度しかなく、
				// 入力を繋いでも指では押せない。ScaleAllSizes が余白と当たり領域を、
				// FontGlobalScale が文字を拡大する(設計書/Android移植設計.md §P7)。
				// iOS も指で操作するので理由は同一(設計書/iOS移植設計.md §5.3)。
				// **倍率が Android と iOS で違うのは、ImGui が扱う座標の意味が違うから。**
				//  - Android: ANativeWindow の**ピクセル**がそのまま来る(2282x1080)。
				//             既定のメニューバーは高さ 20px 程度しかなく指では押せないので 2.0 倍。
				//  - iOS:     contentsScale = 1.0 で動かしているので**論理ポイント**が来る(874x402)。
				//             同じ 2.0 倍にすると画面高さの 1 割をメニューバーが占め、
				//             パネルが画面からはみ出して操作できない(シミュレータで確認)。
				// 画面が狭いほど倍率を抑える必要があるので、共通の定数にはしない。
				{
#if defined(AQ_PLATFORM_IOS)
					constexpr float MOBILE_UI_SCALE = 1.25f;
#else
					constexpr float MOBILE_UI_SCALE = 2.0f;
#endif
					ImGui::GetStyle().ScaleAllSizes(MOBILE_UI_SCALE);
					ImGui::GetIO().FontGlobalScale = MOBILE_UI_SCALE;
				}
#endif

			// Platform 側の入力バックエンド。UWP など持たない実装は既定(成功)を返す。
			// エディタ構成ではゲーム用コンテキストへは繋がない(下でエディタ用コンテキストへ繋ぐ)。
			aq::platform::IPlatform* platform = Engine::Get().GetPlatform();
			const bool winOk = platform && (!GAME_IMGUI_USES_PLATFORM_BACKEND || platform->ImGuiInit());
			const bool backendOk = winOk && aq::graphics::GraphicsDevice::Get().ImGuiInit();
			bool editorOk = true;
#if AQ_EDITOR
			// エディタ用コンテキストの設定とバックエンド接続(win32 と、描画はゲーム用と同じヒープを使う D3D12)。
			// 終わったらゲーム用コンテキストへ戻す。
			if (backendOk)
			{
				ImGui::SetCurrentContext(editorImGuiContext_);
				ImGuiIO& editorIo = ImGui::GetIO();
				editorIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
				// ゲーム用の imgui.ini と分ける。文字列はリテラルなのでプロセス寿命で生きている
				editorIo.IniFilename = "editor_layout.ini";
				ImGui::StyleColorsDark();

				const bool editorWinOk = platform->ImGuiInit();
				editorOk = editorWinOk
				        && aq::graphics::GraphicsDevice::Get().ImGuiInitEditor(gameImGuiContext_, editorImGuiContext_);
				if (!editorOk && editorWinOk) platform->ImGuiShutdown();
				ImGui::SetCurrentContext(gameImGuiContext_);
			}
#endif
			if (backendOk && editorOk)
			{
				imguiReady_ = true;
			}
			else
			{
#if AQ_EDITOR
				// エディタ用を先に壊す(共有アトラスの参照を 1 に落としてから、ゲーム側にフォントを解放させる)
				if (editorImGuiContext_)
				{
					ImGui::DestroyContext(editorImGuiContext_);
					editorImGuiContext_ = nullptr;
					ImGui::SetCurrentContext(gameImGuiContext_);
				}
#endif
				// ゲーム用の描画バックエンドまで繋がってから失敗した(= エディタ側の失敗)ときだけ畳む
				if (backendOk) aq::graphics::GraphicsDevice::Get().ImGuiShutdown();
				if (winOk && GAME_IMGUI_USES_PLATFORM_BACKEND) platform->ImGuiShutdown();
				ImGui::DestroyContext(gameImGuiContext_);
				gameImGuiContext_ = nullptr;
				EngineAssertMsg(false, "ImGui backend initialization failed");
			}
		}
		aq::StartupMark("  [app] ImGui ok (font atlas built, ASCII only)");
#endif // AQ_IMGUI

		// エディタ連携(-editor-port 指定時だけ)。モジュールが OnInitialize で命令を足せるよう、その前に起動する。
		// 待ち受けに失敗してもゲームは動かす(エディタ側からは接続できないだけ)
		{
			const uint16_t editorPort = Engine::Get().GetLaunchOptions().editorPort;
			if (editorPort != 0)
			{
				if (aq::link::LinkService::Initialize(editorPort, module_->GetName()))
				{
					aq::link::RegisterEngineCommands(aq::link::LinkService::Get());
				}
				aq::StartupMarkf("  [app] LinkService %s (port %u)",
				                 aq::link::LinkService::IsAvailable() ? "ok" : "unavailable",
				                 static_cast<uint32_t>(editorPort));
			}
		}

		if (!module_->OnInitialize(*this)) return false;
		aq::StartupMark("  [app] game OnInitialize ok");
#if !AQ_EDITOR
		// エディタ構成は Edit で起動する。OnPlayBegin は Editor の再生ボタンが呼ぶ。
		module_->OnPlayBegin();
		aq::StartupMark("  [app] game OnPlayBegin ok");
#endif

		// GPU 駆動クラスタ(トライアングル)カリング: compute シェーダをロード。
		// compute 非対応(FL10 の Xbox One UWP 等)では初期化せず、カリングも無効化する。
		if (!aq::graphics::IsComputeSupported())
		{
			aq::rendering::SetClusterCullEnabled(false);
		}

		// Hi-Z (オクリュージョン基盤): HiZRenderer の生成・破棄は HiZPass::Setup() が持つ
		// (SetupStandardRenderers / SetRenderPipeline でパイプラインに組み込み済み)。
		// ここでは compute シェーダのロードと、パイプラインに HiZPass があれば
		// その HiZRenderer をオクリュージョンのテスターとして登録するだけ。
		if (aq::graphics::IsComputeSupported())
		{
			aq::rendering::GpuClusterCuller::Get().Initialize();

			if (auto* pipeline = renderer_.GetPipeline())
			{
				if (auto* hiZPass = pipeline->Find<rendering::HiZPass>())
				{
					if (auto* hiZ = hiZPass->GetHiZRenderer())
					{
						// オクリュージョンカリングのテスターとして登録
						aq::ecs::RenderSystem::SetOcclusionTester(hiZ);
						// GPU 駆動クラスタカリングの Hi-Z オクリュージョン供給元として登録
						aq::rendering::GpuClusterCuller::Get().SetHiZSource(hiZ);
					}
				}
			}
		}  // if (aq::graphics::IsComputeSupported())
		aq::StartupMark("  [app] ClusterCull/HiZ ok (CS x4 compiled)");

#ifdef AQ_DEBUG_IMGUI
		// OnInitialize() でゲーム側が Shadow/Bloom 等のレンダラを設定した後にパネルを生成する
		{
			renderingDebugPanel_ = std::make_unique<aq::rendering::RenderingDebugPanel>();

			// Lighting
			{
				auto panel = std::make_unique<aq::rendering::LightingDebugPanel>();
				renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
				renderingDebugPanel_->TakeOwnership(std::move(panel));
			}

			// Ocean
			oceanDebugPanel_ = std::make_unique<aq::ocean::OceanDebugPanel>();
			renderingDebugPanel_->AddTab("Ocean", oceanDebugPanel_.get());

			// Shadow / GBuffer のパネルは、パイプラインに該当パスがあるときだけ生成する。
			auto* shadowPass  = renderer_.GetPipeline() ? renderer_.GetPipeline()->Find<rendering::ShadowPass>()  : nullptr;
			auto* gbufferPass = renderer_.GetPipeline() ? renderer_.GetPipeline()->Find<rendering::GBufferPass>() : nullptr;
			auto* shadowRenderer = shadowPass ? shadowPass->GetShadowRenderer() : nullptr;

			// Shadow
			if (shadowRenderer)
			{
				auto panel = shadowRenderer->CreateDebugPanel();
				if (panel)
				{
					renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
					renderingDebugPanel_->TakeOwnership(std::move(panel));
				}
			}

			// GBuffer / Shadow テクスチャビューア — ディファードが有効な場合のみ
			if (auto* dr = gbufferPass ? gbufferPass->GetDeferredRenderer() : nullptr)
			{
				auto panel = dr->CreateDebugPanel(shadowRenderer);
				if (panel)
				{
					renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
					renderingDebugPanel_->TakeOwnership(std::move(panel));
				}
			}

			// PostProcess (MotionBlur / Bloom / Tonemap) — パイプラインにあるものだけ集めて 1 枚のパネルにする
			// (P2 でポストプロセスが 3 パスに割れたため、PostProcessChain::CreateDebugPanel() 経由ではなく
			// ここで Find<T>() して直接組み立てる。設計書/レンダーパイプライン設計.md §2)。
			if (auto* pipeline = renderer_.GetPipeline())
			{
				auto* motionBlurPass = pipeline->Find<rendering::MotionBlurPass>();
				auto* bloomPass      = pipeline->Find<rendering::BloomPass>();
				auto* tonemapPass    = pipeline->Find<rendering::TonemapPass>();
				if (motionBlurPass || bloomPass || tonemapPass)
				{
					auto panel = std::make_unique<rendering::PostProcessDebugPanel>(
						motionBlurPass ? motionBlurPass->GetEffect() : nullptr,
						bloomPass      ? bloomPass->GetEffect()      : nullptr,
						tonemapPass    ? tonemapPass->GetEffect()    : nullptr);
					renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
					renderingDebugPanel_->TakeOwnership(std::move(panel));
				}
			}

			// Culling (フラスタム / オクリュージョン / クラスタ GPU)。Profiler から移設。
			{
				auto cullingPanel = std::make_unique<aq::rendering::CullingDebugPanel>();
				renderingDebugPanel_->AddTab(cullingPanel->GetDebugLabel(), cullingPanel.get());
				renderingDebugPanel_->TakeOwnership(std::move(cullingPanel));
			}

			aq::DebugUI::Get().Register(renderingDebugPanel_.get());

			// UI エディタパネル
			uiEditorDebugPanel_ = std::make_unique<aq::ui::UIEditorDebugPanel>();
			aq::DebugUI::Get().Register(uiEditorDebugPanel_.get());

			// Profiler パネル
			profilerDebugPanel_ = std::make_unique<aq::profile::ProfilerDebugPanel>();
			aq::DebugUI::Get().Register(profilerDebugPanel_.get());

			// Prefab エディタパネル（全シーン共通・アプリ寿命）
			prefabEditorPanel_ = std::make_unique<aq::ecs::PrefabEditorPanel>();
			aq::DebugUI::Get().Register(prefabEditorPanel_.get());

			// Level エディタパネル（全シーン共通・アプリ寿命）
			levelEditorPanel_ = std::make_unique<aq::level::LevelEditorPanel>();
			aq::DebugUI::Get().Register(levelEditorPanel_.get());

			// Hi-Z 可視化タブ
			if (auto* pipeline = renderer_.GetPipeline())
			{
				if (auto* hiZPass = pipeline->Find<rendering::HiZPass>())
				{
					if (auto* hiZ = hiZPass->GetHiZRenderer())
					{
						auto panel = hiZ->CreateDebugPanel();
						renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
						renderingDebugPanel_->TakeOwnership(std::move(panel));
					}
				}
			}

			// SSAO タブ — パイプラインに SSAOPass があり、レンダラーが生成できているときだけ
			if (auto* pipeline = renderer_.GetPipeline())
			{
				if (auto* ssaoPass = pipeline->Find<rendering::SSAOPass>())
				{
					if (auto* ssao = ssaoPass->GetSSAORenderer())
					{
						auto panel = ssao->CreateDebugPanel();
						renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
						renderingDebugPanel_->TakeOwnership(std::move(panel));
					}
				}
			}

			// SSR タブ — パイプラインに SSRPass があり、レンダラーが生成できているときだけ
			if (auto* pipeline = renderer_.GetPipeline())
			{
				if (auto* ssrPass = pipeline->Find<rendering::SSRPass>())
				{
					if (auto* ssr = ssrPass->GetSSRRenderer())
					{
						auto panel = ssr->CreateDebugPanel();
						renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
						renderingDebugPanel_->TakeOwnership(std::move(panel));
					}
				}
			}

			// DoF タブ — パイプラインに DoFPass があるときだけ(生成失敗で無効でもタブは出す)
			if (auto* pipeline = renderer_.GetPipeline())
			{
				if (auto* dofPass = pipeline->Find<rendering::DoFPass>())
				{
					if (auto* dof = dofPass->GetEffect())
					{
						auto panel = dof->CreateDebugPanel();
						renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
						renderingDebugPanel_->TakeOwnership(std::move(panel));
					}
				}
			}
		}
#endif

		return true;
	}


	bool AppHost::OpenPrefabEditor(const std::string& path)
	{
#ifdef AQ_DEBUG_IMGUI
		if (!prefabEditorPanel_) return false;
		if (!prefabEditorPanel_->OpenFile(path)) return false;
		showDebugUI_ = true;   // DebugUI を隠していても、開いたパネルが見えるようにする
		return true;
#else
		(void)path;
		return false;
#endif
	}


	void AppHost::SetEditorLayer(IEditorLayer* layer)
	{
		editorLayer_ = layer;
#if AQ_EDITOR
		// AppHost の初期化はゲーム用を current にして終わるので、そのままだと最初の PumpEvents の入力が
		// ゲーム用に入る(Mac の公式 osx はイベントが来た時点で current のコンテキストへ入れる)。
		// 2 フレーム目からは FlushRender の末尾でエディタ用にするので、最初のフレームもそれに揃える。
		// フレームの中は Update がゲーム用へ明示的に切り替えるので、ほかの経路は変わらない。
		if (editorLayer_ && editorImGuiContext_) {
			ImGui::SetCurrentContext(editorImGuiContext_);
		}
#endif
	}


	void AppHost::Finalize()
	{
		module_->OnFinalize();

		// 通信スレッドを止める(未送信データは破棄して閉じ、join してから WSACleanup)。
		// 以降のサブシステム破棄中に命令が実行されないよう、モジュールの終了直後に行う
		aq::link::LinkService::Finalize();

		if (renderThreadReady_)
		{
			FlushRender();             // 最後のフレームを描き切ってから停止
			renderThread_.Finalize();
			renderThreadReady_ = false;
		}

		// この下で ImGui / UIContext / ResourceManager / EntityContext が GPU リソースを
		// 破棄していくが、RenderThread の完了待ちは CPU 側(コマンド積み)までしか見ないため、
		// 最後のフレームがまだ GPU で走っていることがある。そのまま壊すと
		//  - Vulkan: validation が「currently in use by VkCommandBuffer」を並べ VMA がアサート(Mac P2 で発覚)
		//  - D3D12 : Present はフェンス Signal のみで待たないため、在フライトの参照先を解放して
		//            終了時に例外(タイミング依存で再現)
		// となるため、API を問わず提出済みの GPU 作業を完了させてから破棄に入る(D3D11 は no-op)。
		aq::graphics::GraphicsDevice::Get().WaitIdle();

#ifdef AQ_IMGUI
		if (imguiReady_)
		{
			aq::platform::IPlatform* platform = Engine::Get().GetPlatform();
#if AQ_EDITOR
			// エディタ用を先に畳む。両コンテキストが生きている間は共有フォントアトラスの参照が 2 のままで、
			// ゲーム側の ImGuiShutdown がフォントのテクスチャとディスクリプタを解放できないため。
			if (editorImGuiContext_)
			{
				ImGui::SetCurrentContext(editorImGuiContext_);
				aq::graphics::GraphicsDevice::Get().ImGuiShutdownEditor();
				// win32 はエディタ用コンテキストにだけ繋いでいる。上の呼び出しが current を変えていてもよいよう指定し直す
				ImGui::SetCurrentContext(editorImGuiContext_);
				if (platform) platform->ImGuiShutdown();
				ImGui::DestroyContext(editorImGuiContext_);
				editorImGuiContext_ = nullptr;
			}
#endif
			ImGui::SetCurrentContext(gameImGuiContext_);
			aq::graphics::GraphicsDevice::Get().ImGuiShutdown();
			if (platform && GAME_IMGUI_USES_PLATFORM_BACKEND)
				platform->ImGuiShutdown();
			ImGui::DestroyContext(gameImGuiContext_);
			gameImGuiContext_ = nullptr;
			imguiReady_ = false;
		}
#endif

#ifdef AQ_DEBUG_IMGUI
		aq::DebugUI::Finalize();
#endif
		// 関数ローカル static のためプロセス終了まで生き残る。GraphicsDevice の破棄より
		// 前にシェーダを手放さないと VkShaderModule がデバイスより長生きする。
		aq::rendering::GpuClusterCuller::Get().Finalize();
		// 名前レジストリもファイルスコープのグローバルで、頂点/インデックスバッファを抱えている。
		aq::graphics::InstancedStaticMesh::ClearNamed();
		aq::ui::UIContext::Finalize();
		aq::graphics::LightManager::Finalize();
		aq::ecs::EntityContext::Finalize();
		aq::res::ResourceManager::Finalize();
		aq::hid::InputManager::Finalize();
		// Initialize で new したきり解放していなかった。シングルトンだが寿命は AppHost に合わせる。
		aq::CameraManager::Finalize();
	}


	void AppHost::Update()
	{
#ifdef AQ_PROFILE_ENABLED
		// 前フレームの計測結果を publish (メイン + アイドル状態のワーカー)。
		// この時点で前フレームの EntityContext::Update() は完了済みでワーカーはアイドル。
		aq::profile::Profiler::Get().PublishThisThread();
		aq::profile::Profiler::Get().PublishWorkers();
#endif
		AQ_PROFILE_SCOPE("AppHost::Update");

#ifdef AQ_IMGUI
		if (imguiReady_)
		{
			// 入力抑制はゲーム用コンテキストで判定する(エディタ構成ではフレームの間にエディタ用が current になっている)
			ImGui::SetCurrentContext(gameImGuiContext_);
			const auto& io = ImGui::GetIO();
			aq::hid::InputManager::Get().SuppressKeyboard(io.WantCaptureKeyboard);
			aq::hid::InputManager::Get().SuppressMouse(io.WantCaptureMouse);
		}
#endif
		// 埋め込み時は、ゲームの子ウィンドウがキーボードフォーカスを持たない間(エディタ側を操作中)の
		// 入力を止める。ImGui の有無に依らず、パッドも含めて効かせるため AQ_IMGUI の外に置く。
		// 通常起動では立てない(フォーカス外でもパッドを読める従来の挙動を保つ)。
		if (Engine::Get().IsEmbedded())
		{
			aq::hid::InputManager::Get().SetFocusBlocked(!Engine::Get().HasInputFocus());
		}
#if AQ_EDITOR
		if (editorLayer_ && imguiReady_ && editorImGuiContext_)
		{
			// エディタ用を明示して current にする(初回の Update は初期化後でゲーム用が current のまま)。
			// win32 の NewFrame をここで呼ぶのは、win32 が補うキーの離しなどを、エディタが入力キューを覗く前に積ませるため
			// (Render() で呼ぶと覗いた後に積まれ、エディタ用の NewFrame で消えてゲーム用ではキーが押されたままになる)
			ImGui::SetCurrentContext(editorImGuiContext_);
			if (aq::platform::IPlatform* platform = Engine::Get().GetPlatform()) {
				platform->ImGuiNewFrame();
			}
			editorLayer_->OnBeginFrame(gameImGuiContext_);
			ImGui::SetCurrentContext(gameImGuiContext_);
		}
#endif
		{ AQ_PROFILE_SCOPE("Input::Update"); aq::hid::InputManager::Get().Update(); }
		{
			AQ_PROFILE_SCOPE("EntityContext::Update");
			aq::ecs::EntityContext::Get().Update(runMode_);
		}
		// Level の非同期ロード進行 / ファイル変更監視は EntityContext::Update 後の安全点で実行する
		// （ForEach/並列システム外・単一スレッド）。ここで即時のエンティティ生成を安全に行える。
		{
			AQ_PROFILE_SCOPE("LevelManager::Tick");
			aq::level::LevelManager::Get().Tick(aq::Engine::GetDeltaTime());
		}
		{
			AQ_PROFILE_SCOPE("ResourceManager::Update");
			aq::res::ResourceManager::Get().Update();
		}
		// エディタからの命令はここ(ECS の反復外・Level の即時生成も安全な点)でだけ実行する
		if (aq::link::LinkService::IsAvailable())
		{
			AQ_PROFILE_SCOPE("LinkService::Tick");
			aq::link::LinkService::Get().Tick();
		}
		if (runMode_ == RunMode::Play)
		{
			AQ_PROFILE_SCOPE("OnUpdate");
			module_->OnUpdate();
		}
		{
			AQ_PROFILE_SCOPE("UI::Update");
			auto& ui = aq::ui::UIContext::Get();
			if (runMode_ == RunMode::Play)
			{
				ui.GetInputSystem().Update(ui.Screens(), aq::hid::InputManager::Get());
				ui.Screens().Update(aq::Engine::GetDeltaTime());
			}
			ui.GetBatchRenderer().CollectRenderItems(ui.Screens());
		}
		{ AQ_PROFILE_SCOPE("Camera::UpdateAll"); aq::CameraManager::Get().UpdateAll(); }
		Render();
	}


	void AppHost::FlushRender()
	{
		if (!renderThreadReady_) return;
#ifdef AQ_RENDER_PIPELINED
		// 非同期: 前フレームの完了だけを待ち、今フレームは実行中のまま次へ進む（1フレーム重複）。
		renderThread_.WaitForPipelinedFrame();
#else
		// 直列: 今フレームの全描画完了を待ってから次へ進む。
		renderThread_.WaitForCompletion();
#endif
#if AQ_EDITOR
		// 次のフレームまでに来るウィンドウメッセージをエディタ用コンテキストへ届ける。
		// win32 の WndProc ハンドラは current のコンテキストへ入力を渡すが、win32 はエディタ用にしか
		// 繋いでいない(ゲーム用が current だと入力が捨てられる)。レンダースレッドも描画中に current を
		// 切り替えるので、その完了を待った後のここで決める。次の Update の先頭でゲーム用へ戻す。
		if (editorLayer_ && editorImGuiContext_) {
			ImGui::SetCurrentContext(editorImGuiContext_);
		}
#endif
	}


	void AppHost::WaitForRenderIdle()
	{
		// CPU 側: 提出済みコマンドリストの実行と Present の呼び出しが終わるまで待つ。
		if (renderThreadReady_) {
			renderThread_.WaitForCompletion();
		}
		// GPU 側: D3D12/Vulkan の Present はフェンスを Signal するだけで完了を待たないため、
		// ここまで来ても GPU はまだリソースを参照していることがある。実行中の全コマンドの
		// 完了を待って初めて、在フライト参照なしで破棄できる状態になる (D3D11 は no-op)。
		aq::graphics::GraphicsDevice::Get().WaitIdle();
	}


	bool AppHost::Register()
	{
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::HierarcicalTransformSystem>();
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::AnimationSystem>();
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::SpawnSystem>();
		aq::ecs::EntityContext::Get().AddSystem<aq::level::LevelStreamSystem>();
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::ParticleSystem>();
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::RenderSystem>();

		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, aq::ecs::HierarcicalTransformSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, aq::ecs::AnimationSystem>();
		// パーティクルはワールド変換確定後に更新し、描画構築前に済ませる
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::ParticleSystem, aq::ecs::HierarcicalTransformSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, aq::ecs::ParticleSystem>();
		// スポナーは親ハンドルを読み、階層変換は無効な親を DetachParent で書き換える。
		// 同じ wave に置くと同一フィールドの読み書きが並走するので、変換確定後に回す。
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::SpawnSystem, aq::ecs::HierarcicalTransformSystem>();

#ifdef AQ_DEBUG_IMGUI
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::SceneHierarchySystem>();
#endif

		module_->OnRegister();

		return aq::ecs::EntityContext::Get().FinalizeRegistration();
	}


	void AppHost::Render()
	{
		AQ_PROFILE_SCOPE("AppHost::Render");
		BuildRenderCommands();

		ImDrawData* editorDrawData = nullptr;
#if AQ_EDITOR
		// エディタの UI はゲームの描画コマンドを組んだ後、提出の前に作る。
		// ゲーム用 ImGui の描画データは確定済みなので、ここでコンテキストを切り替えてよい。
		if (editorLayer_ && imguiReady_ && editorImGuiContext_)
		{
			AQ_PROFILE_SCOPE("EditorLayer");
			ImGui::SetCurrentContext(editorImGuiContext_);
			aq::graphics::GraphicsDevice::Get().ImGuiNewFrameEditor();
			ImGui::NewFrame();
			editorLayer_->OnBuildUI(renderer_.GetOutputRT());
			editorLayer_->OnEndFrame();
			editorDrawData = ImGui::GetDrawData();
			ImGui::SetCurrentContext(gameImGuiContext_);
		}
#endif
		SubmitRenderCommands(editorDrawData);
	}


	void AppHost::BuildRenderCommands()
	{
		const float renderW = static_cast<float>(Engine::Get().GetRenderWidth());
		const float renderH = static_cast<float>(Engine::Get().GetRenderHeight());
		float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		auto* mainCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
		if (mainCamera)
			aq::graphics::LightManager::Get().SetCameraPosition(mainCamera->GetPosition());

#ifdef AQ_IMGUI
		ImDrawData* imguiDrawData = nullptr;
		if (imguiReady_)
		{
			// ゲーム用が current であることを前提にした ImGui::GetIO() などの呼び出しがあるため、取り違えを検出する
			EngineAssertMsg(ImGui::GetCurrentContext() == gameImGuiContext_, "BuildRenderCommands must be called with the game ImGui context current");
			aq::platform::IPlatform* platform = Engine::Get().GetPlatform();
			// エディタ構成ではゲーム用コンテキストに win32 を繋いでいないので、プラットフォームの NewFrame を
			// 呼ばない(未接続のバックエンドを踏んで assert する)。下の自前設定の経路で埋める。
			if (!(GAME_IMGUI_USES_PLATFORM_BACKEND && platform && platform->ImGuiNewFrame()))
			{
				// Platform 側のバックエンドが無い(UWP 等)ので、それが埋めるべき最低限の 2 つを自前で入れる。
				//  - DisplaySize: 0 のままだと ImGui::NewFrame のサニティチェックで停止する
				//  - DeltaTime  : 0 以下だと同じくアサートに掛かる(初回フレームは実測値が無い)
				// エディタ構成のゲーム用コンテキストは displayRT へ描くので、画面ではなくレンダー解像度にする。
				ImGuiIO& io = ImGui::GetIO();
				io.DisplaySize = GAME_IMGUI_USES_PLATFORM_BACKEND
				               ? ImVec2(static_cast<float>(Engine::Get().GetScreenWidth()),
				                        static_cast<float>(Engine::Get().GetScreenHeight()))
				               : ImVec2(renderW, renderH);
				const float deltaTime = Engine::GetDeltaTime();
				io.DeltaTime = (deltaTime > 0.0f) ? deltaTime : (1.0f / 60.0f);
			}
			aq::graphics::GraphicsDevice::Get().ImGuiNewFrame();
			ImGui::NewFrame();

#if defined(AQ_PLATFORM_ANDROID) || defined(AQ_PLATFORM_IOS)
			// 仮想パッドの位置を画面に出す。
			// **暫定の可視化**で、当たり判定と同じレイアウト値を円で描くだけ。
			// これが無いと指をどこへ置けばよいか分からず操作できない。
			// TODO: 実機で操作感を詰めたら、デバッグ描画ではなく UI 層(UIObject)の
			//       正式な見た目へ置き換える。触っている点の表示も、そのとき外す。
			{
				const float screenW = static_cast<float>(Engine::Get().GetScreenWidth());
				const float screenH = static_cast<float>(Engine::Get().GetScreenHeight());

				// 判定側が SetLayout を使っていないので既定値で一致する。
				// レイアウトを動かせるようにしたら、実体から読むよう直すこと。
				const aq::hid::VirtualPadBackend::Layout layout;
				ImDrawList* drawList = ImGui::GetBackgroundDrawList();

				const auto drawPadCircle =
					[&](const aq::hid::VirtualPadBackend::Circle& circle, ImU32 fill, const char* label)
				{
					const ImVec2 center(circle.centerX * screenW, circle.centerY * screenH);
					const float  radius = circle.radius * screenH;
					drawList->AddCircleFilled(center, radius, fill, 32);
					drawList->AddCircle(center, radius, IM_COL32(255, 255, 255, 140), 32, 2.0f);
					if (label)
					{
						const ImVec2 size = ImGui::CalcTextSize(label);
						drawList->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f),
						                  IM_COL32(255, 255, 255, 200), label);
					}
				};

				drawPadCircle(layout.leftStick,   IM_COL32(255, 255, 255,  28), nullptr);
				drawPadCircle(layout.buttonA,     IM_COL32(120, 220, 255,  56), "A");
				drawPadCircle(layout.buttonB,     IM_COL32(255, 200, 120,  56), "B");
				drawPadCircle(layout.buttonStart, IM_COL32(200, 200, 200,  40), "START");

				// 触れている点。押しているのに反応しないときの切り分けに使う。
				const aq::hid::TouchState& touch = aq::hid::InputManager::Get().GetTouchState();
				const uint32_t touchCount = (touch.count < aq::hid::TouchState::MAX_POINT_COUNT)
				                          ? touch.count : aq::hid::TouchState::MAX_POINT_COUNT;
				for (uint32_t i = 0; i < touchCount; ++i)
				{
					if (!touch.points[i].pressed) { continue; }
					drawList->AddCircleFilled(ImVec2(touch.points[i].x, touch.points[i].y),
					                          24.0f, IM_COL32(255, 80, 80, 150), 24);
				}
			}
#endif // AQ_PLATFORM_ANDROID || AQ_PLATFORM_IOS

			// FPS オーバーレイ (常時表示・左上)
			{
				const float fps = aq::Engine::GetFPS();
				const float ms  = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
				ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
				ImGui::SetNextWindowBgAlpha(0.35f);
				const ImGuiWindowFlags flags =
					ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
					ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
					ImGuiWindowFlags_NoMove;
				if (ImGui::Begin("##FPSOverlay", nullptr, flags))
				{
					const char* backend = aq::graphics::GraphicsDevice::Get().GetBackendName();
					ImGui::Text("%s  %.1f FPS (%.2f ms)", backend, fps, ms);
#ifdef AQ_DEBUG_IMGUI
					// トグル操作のヒント（非表示中は重いデバッグ描画がスキップされる）。
#if defined(AQ_PLATFORM_ANDROID) || defined(AQ_PLATFORM_IOS)
					// タッチ環境にはキーボードも中クリックも無いので、指の操作だけを案内する。
					ImGui::TextDisabled(showDebugUI_ ? "4-finger double tap: hide Debug UI" : "4-finger double tap: show Debug UI");
#else
					ImGui::TextDisabled(showDebugUI_ ? "F1 / Middle click: hide Debug UI" : "F1 / Middle click: show Debug UI");
#endif
#endif

					// VSync を実機で切り替えられるバックエンドだけチェックボックスを出す
					if (aq::graphics::GraphicsDevice::Get().CanToggleVSync())
					{
						bool vsync = aq::graphics::GraphicsDevice::Get().GetVSync();
						if (ImGui::Checkbox("VSync", &vsync)) aq::graphics::GraphicsDevice::Get().SetVSync(vsync);
					}
				}
				ImGui::End();
			}

			{ AQ_PROFILE_SCOPE("OnImGuiRender"); module_->OnImGuiRender(); }

#ifdef AQ_DEBUG_IMGUI
			// デバッグ UI 表示トグル: 中クリック or F1。非表示中は下の重い ECS::DebugRender / 各パネル描画を
			// 完全にスキップする（毎フレーム 100+ エンティティを ImGui 描画する処理が止まり大幅に軽くなる）。
			// F1 は ImGui のキー状態で判定し、ゲーム入力抑制（WantCaptureKeyboard）の影響を受けない。
			//
			// スマホにはキーボードも中クリックも無いので、**指を 4 本そろえたダブルタップ**を
			// 割り当てる。通常操作(仮想スティック 1 本 + ボタン 1 本)と本数が被らないので
			// 誤爆しにくい。タッチを持たないプラットフォームでは TouchState が常に空なので
			// この項は必ず false になり、挙動は変わらない。
			static aq::hid::MultiTouchDoubleTapDetector s_debugUiToggleGesture;
			const bool toggleByTouch = s_debugUiToggleGesture.Update(
				aq::hid::InputManager::Get().GetTouchState(), Engine::GetDeltaTime());

			if (ImGui::GetIO().MouseClicked[2] || ImGui::IsKeyPressed(ImGuiKey_F1, false) || toggleByTouch)
				showDebugUI_ = !showDebugUI_;

			if (showDebugUI_)
			{
				AQ_PROFILE_SCOPE("DebugUI");
				if (ImGui::BeginMainMenuBar())
				{
					aq::ecs::EntityContext::Get().DebugRenderMenu();
					aq::DebugUI::Get().DebugRenderMenuAll();
					module_->OnDebugRenderMenu();
					// ImGui 標準の Metrics ウィンドウ。Inputs 欄で押下中のキーを見られる
					// (Mac の Command 押下中の keyUp 欠落の確認用。エディタ全体設計.md 4.5 節)。
					if (ImGui::BeginMenu("ImGui"))
					{
						ImGui::MenuItem("Metrics", nullptr, &showImGuiMetrics_);
						ImGui::EndMenu();
					}
					ImGui::EndMainMenuBar();
				}
				if (showImGuiMetrics_) ImGui::ShowMetricsWindow(&showImGuiMetrics_);
				{ AQ_PROFILE_SCOPE("ECS::DebugRender"); aq::ecs::EntityContext::Get().DebugRender(); }
				aq::DebugUI::Get().DebugRenderAll();
				{ AQ_PROFILE_SCOPE("OnDebugRender"); module_->OnDebugRender(); }
			}
#endif

			{ AQ_PROFILE_SCOPE("ImGui::Render"); ImGui::Render(); imguiDrawData = ImGui::GetDrawData(); }
		}
#endif

		if (runMode_ != RunMode::Edit)
		{
			AQ_PROFILE_SCOPE("OnPreRender");
			module_->OnPreRender();
		}

		auto mainCmdList = std::make_unique<aq::rendering::RenderCommandList>();
		mainCmdList->Enqueue<aq::rendering::SetRenderTargetCommand>(Engine::Get().GetMainRenderTargetHandle());
		mainCmdList->Enqueue<aq::rendering::ClearRenderTargetCommand>(0u, clearColor);
		mainCmdList->Enqueue<aq::rendering::ClearDepthCommand>();
		mainCmdList->Enqueue<aq::rendering::SetViewportCommand>(0.0f, 0.0f, renderW, renderH);
		aq::rendering::RenderFrame mainFrame;
		mainFrame.lighting = aq::graphics::LightManager::Get().GetLightingData();
		if (splitViews_.size() >= 2)
		{
			// 分割画面: ビュー毎に RenderFrame を構築し、1 本のリストへマルチビュー記録する。
			// Hi-Z オクリュージョンは単一カメラ前提のため無効、統計は先頭ビューのみ更新。
			// インスタンス gather + Flush も 1 フレーム 1 回に保つため先頭ビューのみで行い、
			// 他ビューはビュー0 の視錐台で切った結果を共有する(パーティクルと同じ制限)。
			AQ_PROFILE_SCOPE("BuildRenderFrameViews");
			constexpr uint32_t MAX_VIEW_COUNT = 4;
			const uint32_t viewCount = splitViews_.size() < MAX_VIEW_COUNT
				? static_cast<uint32_t>(splitViews_.size()) : MAX_VIEW_COUNT;

			aq::rendering::RenderFrame           viewFrames[MAX_VIEW_COUNT];
			aq::rendering::ViewRect              viewRects[MAX_VIEW_COUNT];
			for (uint32_t v = 0; v < viewCount; ++v)
			{
				viewFrames[v].lighting = mainFrame.lighting;
				aq::ecs::RenderSystem::Get().BuildRenderFrame(
					viewFrames[v], *splitViews_[v].camera,
					true /*frustum*/, false /*occlusion*/, v == 0 /*stats*/, v == 0 /*gather*/);
				viewRects[v] = splitViews_[v].rect;
			}
			if (auto* pipeline = renderer_.GetPipeline())
			{
				pipeline->BuildViews(viewFrames, viewRects, viewCount, *mainCmdList,
				                     Engine::Get().GetMainRenderTargetHandle(), renderW, renderH);
			}

			// Submit に渡す per-frame CB (b1/b3) はビュー共有 (シャドウは view0 で確定)。
			mainFrame.shadow = viewFrames[0].shadow;
		}
		else
		{
			{
				AQ_PROFILE_SCOPE("BuildRenderFrame");
				aq::ecs::RenderSystem::Get().BuildRenderFrame(mainFrame);
			}
			{
				AQ_PROFILE_SCOPE("BuildCommandList");
				renderer_.BuildCommandList(mainFrame, *mainCmdList,
				                          Engine::Get().GetMainRenderTargetHandle(), renderW, renderH);
			}
		}
#ifdef AQ_IMGUI
		if (imguiDrawData)
			mainCmdList->Enqueue<aq::rendering::ImGuiRenderCommand>(imguiDrawData);
#endif
		pendingCmdList_  = std::move(mainCmdList);
		pendingLighting_ = mainFrame.lighting;
		pendingShadow_   = mainFrame.shadow;
	}


	void AppHost::SubmitRenderCommands(ImDrawData* editorDrawData)
	{
		AQ_PROFILE_SCOPE("Submit"); renderThread_.Submit(std::move(pendingCmdList_), renderer_.GetOutputRT(),
		                    pendingLighting_, pendingShadow_, editorDrawData);
	}
}
