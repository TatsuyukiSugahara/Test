#include "aq.h"
#include "Application.h"
#include "ECS/EntityContext.h"
#include "HID/Input.h"
#if defined(AQ_PLATFORM_ANDROID)
#include "HID/VirtualPadBackend.h"   // 仮想パッドの当たり判定レイアウト(暫定の可視化用)
#endif
#include "UI/Input/UIInputSystem.h"
#include "UI/Rendering/UIBatchRenderer.h"
#include "Component/AnimationComponentSystem.h"
#include "Component/ParticleComponentSystem.h"
#include "ECS/SpawnSystem.h"
#include "Util/Profiler.h"
#include "Rendering/Occlusion/HiZRenderer.h"
#include "Rendering/Occlusion/GpuClusterCuller.h"
#include "Rendering/Occlusion/ClusterCull.h"   // SetClusterCullEnabled
#include "Graphics/InstancedStaticMesh.h"      // Finalize での名前レジストリ解放
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Occlusion/Debug/CullingDebugPanel.h"
#endif
#ifdef AQ_IMGUI
#include <imgui/imgui.h>
#if defined(AQ_PLATFORM_WIN32)
#include <imgui/imgui_impl_win32.h>
#elif defined(AQ_PLATFORM_MAC)
#include "Platform/Mac/MacImGui.h"
#endif
#include "Rendering/ImGuiRenderCommand.h"
#ifdef ENGINE_GRAPHICS_D3D11
#include "Graphics/D3D11/D3D11GraphicsDeviceImpl.h"
#include <imgui/imgui_impl_dx11.h>
#elif defined(ENGINE_GRAPHICS_D3D12)
#include "Graphics/D3D12/D3D12ImGui.h"
#include "Graphics/D3D12/D3D12GraphicsDeviceImpl.h"
#elif defined(ENGINE_GRAPHICS_VULKAN)
#include "Graphics/Vulkan/VulkanImGui.h"
#elif defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalImGui.h"
#endif
#endif
#include "ECS/ComponentRegistry.h"   // JSON シリアライズ用。常時コンパイル（AQ_DEBUG_IMGUI 非依存）。
#include "Level/LevelComponentRegistry.h"
#include "Level/LevelStreamSystem.h"
#include "Level/LevelManager.h"
#ifdef AQ_DEBUG_IMGUI
#include "DebugUI.h"
#include "Ocean/Debug/OceanDebugPanel.h"
#include "Rendering/Debug/RenderingDebugPanel.h"
#include "ECS/SceneHierarchySystem.h"
#include "UI/Debug/UIEditorDebugPanel.h"
#include "UI/Debug/TextStyleEditorPanel.h"
#include "UI/Debug/UIAnimationEditor.h"
#endif


namespace aq
{
	bool Application::Initialize(aq::graphics::RenderContext& renderContext)
	{
#ifdef AQ_PROFILE_ENABLED
		aq::profile::Profiler::Get().SetThreadName("Main");
#endif
		renderThread_.Initialize(&renderContext);
		renderThreadReady_ = true;

		aq::res::ResourceManager::Initialize();
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
			ImGui::CreateContext();

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

#if defined(AQ_PLATFORM_WIN32)
			const bool winOk = ImGui_ImplWin32_Init(Engine::Get().GetHWND());
#elif defined(AQ_PLATFORM_MAC)
			const bool winOk = aq::platform::MacImGui::Init();
#else
			// UWP はプラットフォームバックエンドを持たない(ImGui へ入力が届かない)。
			const bool winOk = true;
#endif
			bool backendOk = false;
#ifdef ENGINE_GRAPHICS_D3D11
			auto* d3d = dynamic_cast<aq::graphics::D3D11GraphicsDeviceImpl*>(
				aq::graphics::GraphicsDevice::Get().GetImplRaw());
			EngineAssertMsg(d3d != nullptr, "AQ_IMGUI requires D3D11GraphicsDeviceImpl");
			backendOk = winOk && d3d && ImGui_ImplDX11_Init(d3d->GetDevice(), d3d->GetDeviceContext());
#elif defined(ENGINE_GRAPHICS_D3D12)
			backendOk = winOk && aq::graphics::D3D12ImGui::Init();
#elif defined(ENGINE_GRAPHICS_VULKAN)
			backendOk = winOk && aq::graphics::VulkanImGui::Init();
#elif defined(ENGINE_GRAPHICS_METAL)
			backendOk = winOk && aq::graphics::MetalImGui::Init();
#endif
			if (backendOk)
			{
				imguiReady_ = true;
			}
			else
			{
#if defined(AQ_PLATFORM_WIN32)
				if (winOk) ImGui_ImplWin32_Shutdown();
#elif defined(AQ_PLATFORM_MAC)
				if (winOk) aq::platform::MacImGui::Shutdown();
#endif
				ImGui::DestroyContext();
				EngineAssertMsg(false, "ImGui backend initialization failed");
			}
		}
		aq::StartupMark("  [app] ImGui ok (font atlas built, ASCII only)");
#endif // AQ_IMGUI

		renderer_.SetUIRenderCallback([](aq::rendering::RenderCommandList& list) {
			aq::ui::UIContext::Get().GetBatchRenderer().BuildCommandList(list);
		});

		if (!OnInitialize()) return false;
		aq::StartupMark("  [app] game OnInitialize ok");

		// GPU 駆動クラスタ(トライアングル)カリング: compute シェーダをロード。
		// compute 非対応(FL10 の Xbox One UWP 等)では初期化せず、カリングも無効化する。
		if (!aq::graphics::IsComputeSupported())
		{
			aq::rendering::SetClusterCullEnabled(false);
		}

		// Hi-Z (オクリュージョン基盤): ディファードが有効なときのみ。
		// G-Buffer の worldPos から深度ピラミッドを構築する。
		if (aq::graphics::IsComputeSupported())
		{
		aq::rendering::GpuClusterCuller::Get().Initialize();
		if (auto* dr = dynamic_cast<rendering::DeferredRenderer*>(renderer_.GetDeferredRenderer()))
		{
			hiZRenderer_ = std::make_unique<rendering::HiZRenderer>();
			if (hiZRenderer_->Initialize(Engine::Get().GetRenderWidth(), Engine::Get().GetRenderHeight()))
			{
				const rendering::RenderTargetHandle gb2 = dr->GetGBuffer2Handle();
				auto* hiZ = hiZRenderer_.get();
				renderer_.SetHiZBuildCallback(
					[hiZ, gb2](const rendering::RenderFrame& f, rendering::RenderCommandList& l)
					{
						hiZ->BuildCommandList(f, l, gb2);
					});
				// オクリュージョンカリングのテスターとして登録
				aq::ecs::RenderSystem::SetOcclusionTester(hiZ);
				// GPU 駆動クラスタカリングの Hi-Z オクリュージョン供給元として登録
				aq::rendering::GpuClusterCuller::Get().SetHiZSource(hiZ);
			}
			else
			{
				hiZRenderer_.reset();
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

			// Shadow — ゲームが SetShadowRenderer していれば自動でパネルを生成
			if (auto* sr = renderer_.GetShadowRenderer())
			{
				auto panel = sr->CreateDebugPanel();
				if (panel)
				{
					renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
					renderingDebugPanel_->TakeOwnership(std::move(panel));
				}
			}

			// GBuffer / Shadow テクスチャビューア — ディファードが有効な場合のみ
			if (auto* dr = dynamic_cast<rendering::DeferredRenderer*>(renderer_.GetDeferredRenderer()))
			{
				auto* sr = renderer_.GetShadowRenderer();
				auto panel = dr->CreateDebugPanel(sr);
				if (panel)
				{
					renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
					renderingDebugPanel_->TakeOwnership(std::move(panel));
				}
			}

			// PostProcess (Bloom など) — 同上
			if (auto* pp = renderer_.GetPostProcessRenderer())
			{
				auto panel = pp->CreateDebugPanel();
				if (panel)
				{
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

			// TextStyle エディタパネル
			textStyleEditorPanel_ = std::make_unique<aq::ui::TextStyleEditorPanel>();
			aq::DebugUI::Get().Register(textStyleEditorPanel_.get());

			// UI Animation Editor
			uiAnimationEditor_ = std::make_unique<aq::ui::UIAnimationEditor>();
			aq::DebugUI::Get().Register(uiAnimationEditor_.get());

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
			if (hiZRenderer_)
			{
				auto panel = hiZRenderer_->CreateDebugPanel();
				renderingDebugPanel_->AddTab(panel->GetDebugLabel(), panel.get());
				renderingDebugPanel_->TakeOwnership(std::move(panel));
			}
		}
#endif

		return true;
	}


	void Application::Finalize()
	{
		OnFinalize();

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
#ifdef ENGINE_GRAPHICS_D3D11
			ImGui_ImplDX11_Shutdown();
#elif defined(ENGINE_GRAPHICS_D3D12)
			aq::graphics::D3D12ImGui::Shutdown();
#elif defined(ENGINE_GRAPHICS_VULKAN)
			aq::graphics::VulkanImGui::Shutdown();
#elif defined(ENGINE_GRAPHICS_METAL)
			aq::graphics::MetalImGui::Shutdown();
#endif
#if defined(AQ_PLATFORM_WIN32)
			ImGui_ImplWin32_Shutdown();
#elif defined(AQ_PLATFORM_MAC)
			aq::platform::MacImGui::Shutdown();
#endif
			ImGui::DestroyContext();
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
	}


	void Application::Update()
	{
#ifdef AQ_PROFILE_ENABLED
		// 前フレームの計測結果を publish (メイン + アイドル状態のワーカー)。
		// この時点で前フレームの EntityContext::Update() は完了済みでワーカーはアイドル。
		aq::profile::Profiler::Get().PublishThisThread();
		aq::profile::Profiler::Get().PublishWorkers();
#endif
		AQ_PROFILE_SCOPE("Application::Update");

#ifdef AQ_IMGUI
		if (imguiReady_)
		{
			const auto& io = ImGui::GetIO();
			aq::hid::InputManager::Get().SuppressKeyboard(io.WantCaptureKeyboard);
			aq::hid::InputManager::Get().SuppressMouse(io.WantCaptureMouse);
		}
#endif
		{ AQ_PROFILE_SCOPE("Input::Update"); aq::hid::InputManager::Get().Update(); }
		{
			AQ_PROFILE_SCOPE("EntityContext::Update");
			aq::ecs::EntityContext::Get().Update();
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
		{ AQ_PROFILE_SCOPE("OnUpdate"); OnUpdate(); }
		{
			AQ_PROFILE_SCOPE("UI::Update");
			auto& ui = aq::ui::UIContext::Get();
			ui.GetInputSystem().Update(ui.Screens(), aq::hid::InputManager::Get());
			ui.Screens().Update(aq::Engine::GetDeltaTime());
			ui.GetBatchRenderer().CollectRenderItems(ui.Screens());
		}
		{ AQ_PROFILE_SCOPE("Camera::UpdateAll"); aq::CameraManager::Get().UpdateAll(); }
		Render();
	}


	void Application::FlushRender()
	{
		if (!renderThreadReady_) return;
#ifdef AQ_RENDER_PIPELINED
		// 非同期: 前フレームの完了だけを待ち、今フレームは実行中のまま次へ進む（1フレーム重複）。
		renderThread_.WaitForPipelinedFrame();
#else
		// 直列: 今フレームの全描画完了を待ってから次へ進む。
		renderThread_.WaitForCompletion();
#endif
	}


	void Application::WaitForRenderIdle()
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


	void Application::Register()
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

#ifdef AQ_DEBUG_IMGUI
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::SceneHierarchySystem>();
#endif

		OnRegister();

		aq::ecs::EntityContext::Get().FinalizeRegistration();
	}


	void Application::Render()
	{
		AQ_PROFILE_SCOPE("Application::Render");
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
#if defined(AQ_PLATFORM_WIN32)
			ImGui_ImplWin32_NewFrame();
#elif defined(AQ_PLATFORM_MAC)
			aq::platform::MacImGui::NewFrame();
#else
			// UWP はプラットフォームバックエンドが無いので、それが埋めるべき最低限の 2 つを自前で入れる。
			//  - DisplaySize: 0 のままだと ImGui::NewFrame のサニティチェックで停止する
			//  - DeltaTime  : 0 以下だと同じくアサートに掛かる(初回フレームは実測値が無い)
			{
				ImGuiIO& io = ImGui::GetIO();
				io.DisplaySize = ImVec2(static_cast<float>(Engine::Get().GetScreenWidth()),
				                        static_cast<float>(Engine::Get().GetScreenHeight()));
				const float deltaTime = Engine::GetDeltaTime();
				io.DeltaTime = (deltaTime > 0.0f) ? deltaTime : (1.0f / 60.0f);
			}
#endif
#ifdef ENGINE_GRAPHICS_D3D11
			ImGui_ImplDX11_NewFrame();
#elif defined(ENGINE_GRAPHICS_D3D12)
			aq::graphics::D3D12ImGui::NewFrame();
#elif defined(ENGINE_GRAPHICS_VULKAN)
			aq::graphics::VulkanImGui::NewFrame();
#elif defined(ENGINE_GRAPHICS_METAL)
			aq::graphics::MetalImGui::NewFrame();
#endif
			ImGui::NewFrame();

#if defined(AQ_PLATFORM_ANDROID)
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
#endif // AQ_PLATFORM_ANDROID

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
#if defined(ENGINE_GRAPHICS_D3D12)
					const char* backend = "D3D12";
#elif defined(ENGINE_GRAPHICS_D3D11)
					const char* backend = "D3D11";
#elif defined(ENGINE_GRAPHICS_VULKAN)
					const char* backend = "Vulkan";
#elif defined(ENGINE_GRAPHICS_METAL)
					const char* backend = "Metal";
#else
					const char* backend = "?";
#endif
					ImGui::Text("%s  %.1f FPS (%.2f ms)", backend, fps, ms);
#ifdef AQ_DEBUG_IMGUI
					// トグル操作のヒント（非表示中は重いデバッグ描画がスキップされる）。
					ImGui::TextDisabled(showDebugUI_ ? "F1 / Middle click: hide Debug UI" : "F1 / Middle click: show Debug UI");
#endif

#if defined(ENGINE_GRAPHICS_D3D12)
					// VSync を実機で切り替えられるようにする
					auto* d3d12 = static_cast<aq::graphics::D3D12GraphicsDeviceImpl*>(
						aq::graphics::GraphicsDevice::Get().GetImplRaw());
					if (d3d12)
					{
						bool vsync = d3d12->GetVSync();
						if (ImGui::Checkbox("VSync", &vsync)) d3d12->SetVSync(vsync);
					}
#endif
				}
				ImGui::End();
			}

			{ AQ_PROFILE_SCOPE("OnImGuiRender"); OnImGuiRender(); }

#ifdef AQ_DEBUG_IMGUI
			// デバッグ UI 表示トグル: 中クリック or F1。非表示中は下の重い ECS::DebugRender / 各パネル描画を
			// 完全にスキップする（毎フレーム 100+ エンティティを ImGui 描画する処理が止まり大幅に軽くなる）。
			// F1 は ImGui のキー状態で判定し、ゲーム入力抑制（WantCaptureKeyboard）の影響を受けない。
			if (ImGui::GetIO().MouseClicked[2] || ImGui::IsKeyPressed(ImGuiKey_F1, false))
				showDebugUI_ = !showDebugUI_;

			if (showDebugUI_)
			{
				AQ_PROFILE_SCOPE("DebugUI");
				if (ImGui::BeginMainMenuBar())
				{
					aq::ecs::EntityContext::Get().DebugRenderMenu();
					aq::DebugUI::Get().DebugRenderMenuAll();
					OnDebugRenderMenu();
					ImGui::EndMainMenuBar();
				}
				{ AQ_PROFILE_SCOPE("ECS::DebugRender"); aq::ecs::EntityContext::Get().DebugRender(); }
				aq::DebugUI::Get().DebugRenderAll();
				{ AQ_PROFILE_SCOPE("OnDebugRender"); OnDebugRender(); }
			}
#endif

			{ AQ_PROFILE_SCOPE("ImGui::Render"); ImGui::Render(); imguiDrawData = ImGui::GetDrawData(); }
		}
#endif

		{ AQ_PROFILE_SCOPE("OnPreRender"); OnPreRender(); }

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
			aq::rendering::Renderer::ViewRect    viewRects[MAX_VIEW_COUNT];
			for (uint32_t v = 0; v < viewCount; ++v)
			{
				viewFrames[v].lighting = mainFrame.lighting;
				aq::ecs::RenderSystem::Get().BuildRenderFrame(
					viewFrames[v], *splitViews_[v].camera,
					true /*frustum*/, false /*occlusion*/, v == 0 /*stats*/, v == 0 /*gather*/);
				viewRects[v] = splitViews_[v].rect;
			}
			renderer_.BuildCommandListViews(viewFrames, viewRects, viewCount, *mainCmdList,
			                                Engine::Get().GetMainRenderTargetHandle(), renderW, renderH);

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
		const auto sceneRT = Engine::Get().GetMainRenderTargetHandle();
		AQ_PROFILE_SCOPE("Submit"); renderThread_.Submit(std::move(mainCmdList), renderer_.GetDisplayRTHandle(sceneRT),
		                    mainFrame.lighting, mainFrame.shadow);
	}
}
