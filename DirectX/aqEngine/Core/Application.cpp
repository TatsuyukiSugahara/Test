#include "aq.h"
#include "Application.h"
#include "Engine.h"
#include "RenderConfig.h"
#include "Resource/Resource.h"
#include "ECS/EntityContext.h"
#include "HID/Input.h"
#include "UI/UIContext.h"
#include "UI/Input/UIInputSystem.h"
#include "UI/Screen/UIScreenManager.h"
#include "UI/Rendering/UIBatchRenderer.h"
#include "Graphics/Camera.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/LightManager.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/FrameCommands.h"
#include "Component/TransformComponentSystem.h"
#include "Component/HierarchicalTransformComponent.h"
#include "Component/BodyComponentSystem.h"
#include "Component/AnimationComponentSystem.h"
#include "Util/Profiler.h"
#include "Rendering/Deferred/DeferredRenderer.h"
#include "Rendering/Occlusion/HiZRenderer.h"
#include "Rendering/Occlusion/GpuClusterCuller.h"
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Occlusion/Debug/CullingDebugPanel.h"
#endif
#ifdef AQ_IMGUI
#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include "Rendering/ImGuiRenderCommand.h"
#ifdef ENGINE_GRAPHICS_D3D11
#include "Graphics/D3D11/D3D11GraphicsDeviceImpl.h"
#include <imgui/imgui_impl_dx11.h>
#elif defined(ENGINE_GRAPHICS_D3D12)
#include "Graphics/D3D12/D3D12ImGui.h"
#include "Graphics/D3D12/D3D12GraphicsDeviceImpl.h"
#elif defined(ENGINE_GRAPHICS_VULKAN)
#include "Graphics/Vulkan/VulkanImGui.h"
#endif
#endif
#ifdef AQ_DEBUG_IMGUI
#include "DebugUI.h"
#include "Ocean/Debug/OceanDebugPanel.h"
#include "Rendering/Debug/RenderingDebugPanel.h"
#include "Rendering/Deferred/DeferredRenderer.h"
#include "ECS/ComponentRegistry.h"
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
		if (FAILED(aq::hid::InputManager::Get().Setup()))
		{
			EngineAssertMsg(false, "InputManager::Setup failed: DirectInput or device initialization failed");
			return false;
		}
		aq::ui::UIContext::Initialize();
		aq::CameraManager::Initialize();
		aq::graphics::LightManager::Initialize();
#ifdef AQ_DEBUG_IMGUI
		aq::DebugUI::Initialize();
		aq::ecs::ComponentRegistry::RegisterCoreComponents();
#endif

#ifdef AQ_IMGUI
		{
			ImGui::CreateContext();

				// 日本語フォントを Windows フォントフォルダから読み込む
				// GetWindowsDirectoryA でフォントパスを動的に解決する
				{
					char winDir[MAX_PATH] = {};
					if (GetWindowsDirectoryA(winDir, MAX_PATH) == 0)
						strcpy_s(winDir, "C:\\Windows");

					static const char* kJpFontNames[] = {
						"meiryo.ttc",    // Meiryo (Vista+、推奨)
						"YuGothR.ttc",   // Yu Gothic Regular (Win8.1+)
						"msgothic.ttc",  // MS Gothic (XP+、フォールバック)
					};

					ImGuiIO& io = ImGui::GetIO();
					bool fontLoaded = false;
					for (const char* name : kJpFontNames)
					{
						char path[MAX_PATH];
						sprintf_s(path, "%s\\Fonts\\%s", winDir, name);

						FILE* f = nullptr;
						if (fopen_s(&f, path, "rb") != 0 || !f) continue;
						fclose(f);

						// 日本語 + Arrows (U+2190-21FF) + Geometric Shapes (U+25A0-25FF) を追加
						static const ImWchar kCustomRanges[] = {
							0x0020, 0x00FF, // Basic Latin + Latin-1
							0x2190, 0x21FF, // Arrows (→←↑↓↖↗↘↙↕ 等)
							0x25A0, 0x25FF, // Geometric Shapes (●▶◀▲▼ 等)
							0x3000, 0x30FF, // CJK Symbols, Hiragana, Katakana
							0x31F0, 0x31FF, // Katakana Phonetic Extensions
							0xFF00, 0xFFEF, // Half-width
							0x4e00, 0x9FAF, // CJK Unified Ideographs
							0,
						};
						ImFontConfig cfg;
						cfg.FontNo = 0;  // TTC コレクション内の先頭フォントを使用
						ImFont* font = io.Fonts->AddFontFromFileTTF(
							path, 15.0f, &cfg, kCustomRanges);
						if (font) { fontLoaded = true; break; }
					}
					if (!fontLoaded)
						io.Fonts->AddFontDefault();
				}

			const bool winOk = ImGui_ImplWin32_Init(Engine::Get().GetHWND());
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
#endif
			if (backendOk)
			{
				imguiReady_ = true;
			}
			else
			{
				if (winOk) ImGui_ImplWin32_Shutdown();
				ImGui::DestroyContext();
				EngineAssertMsg(false, "ImGui backend initialization failed");
			}
		}
#endif

		renderer_.SetUIRenderCallback([](aq::rendering::RenderCommandList& list) {
			aq::ui::UIContext::Get().GetBatchRenderer().BuildCommandList(list);
		});

		if (!OnInitialize()) return false;

		// GPU 駆動クラスタ(トライアングル)カリング: compute シェーダをロード
		aq::rendering::GpuClusterCuller::Get().Initialize();

		// Hi-Z (オクリュージョン基盤): ディファードが有効なときのみ。
		// G-Buffer の worldPos から深度ピラミッドを構築する。
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

#ifdef AQ_IMGUI
		if (imguiReady_)
		{
#ifdef ENGINE_GRAPHICS_D3D11
			ImGui_ImplDX11_Shutdown();
#elif defined(ENGINE_GRAPHICS_D3D12)
			aq::graphics::D3D12ImGui::Shutdown();
#elif defined(ENGINE_GRAPHICS_VULKAN)
			aq::graphics::VulkanImGui::Shutdown();
#endif
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			imguiReady_ = false;
		}
#endif

#ifdef AQ_DEBUG_IMGUI
		aq::DebugUI::Finalize();
#endif
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
		aq::hid::InputManager::Get().Update();
		{
			AQ_PROFILE_SCOPE("EntityContext::Update");
			aq::ecs::EntityContext::Get().Update();
		}
		{
			AQ_PROFILE_SCOPE("ResourceManager::Update");
			aq::res::ResourceManager::Get().Update();
		}
		OnUpdate();
		{
			AQ_PROFILE_SCOPE("UI::Update");
			auto& ui = aq::ui::UIContext::Get();
			ui.GetInputSystem().Update(ui.Screens(), aq::hid::InputManager::Get());
			ui.Screens().Update(aq::Engine::GetDeltaTime());
			ui.GetBatchRenderer().CollectRenderItems(ui.Screens());
		}
		aq::CameraManager::Get().UpdateAll();
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


	void Application::Register()
	{
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::HierarcicalTransformSystem>();
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::AnimationSystem>();
		aq::ecs::EntityContext::Get().AddSystem<aq::ecs::RenderSystem>();

		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, aq::ecs::HierarcicalTransformSystem>();
		aq::ecs::EntityContext::Get().AddDependency<aq::ecs::RenderSystem, aq::ecs::AnimationSystem>();

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
			ImGui_ImplWin32_NewFrame();
#ifdef ENGINE_GRAPHICS_D3D11
			ImGui_ImplDX11_NewFrame();
#elif defined(ENGINE_GRAPHICS_D3D12)
			aq::graphics::D3D12ImGui::NewFrame();
#elif defined(ENGINE_GRAPHICS_VULKAN)
			aq::graphics::VulkanImGui::NewFrame();
#endif
			ImGui::NewFrame();

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
#else
					const char* backend = "?";
#endif
					ImGui::Text("%s  %.1f FPS (%.2f ms)", backend, fps, ms);

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

			OnImGuiRender();

#ifdef AQ_DEBUG_IMGUI
			if (ImGui::GetIO().MouseClicked[2])
				showDebugUI_ = !showDebugUI_;

			if (showDebugUI_)
			{
				if (ImGui::BeginMainMenuBar())
				{
					aq::ecs::EntityContext::Get().DebugRenderMenu();
					aq::ecs::EntityContext::Get().DebugRenderSystemMenus();
					aq::DebugUI::Get().DebugRenderMenuAll();
					OnDebugRenderMenu();
					ImGui::EndMainMenuBar();
				}
				aq::ecs::EntityContext::Get().DebugRender();
				aq::DebugUI::Get().DebugRenderAll();
				OnDebugRender();
			}
#endif

			ImGui::Render();
			imguiDrawData = ImGui::GetDrawData();
		}
#endif

		OnPreRender();

		auto mainCmdList = std::make_unique<aq::rendering::RenderCommandList>();
		mainCmdList->Enqueue<aq::rendering::SetRenderTargetCommand>(Engine::Get().GetMainRenderTargetHandle());
		mainCmdList->Enqueue<aq::rendering::ClearRenderTargetCommand>(0u, clearColor);
		mainCmdList->Enqueue<aq::rendering::ClearDepthCommand>();
		mainCmdList->Enqueue<aq::rendering::SetViewportCommand>(0.0f, 0.0f, renderW, renderH);
		aq::rendering::RenderFrame mainFrame;
		mainFrame.lighting = aq::graphics::LightManager::Get().GetLightingData();
		{
			AQ_PROFILE_SCOPE("BuildRenderFrame");
			aq::ecs::RenderSystem::Get().BuildRenderFrame(mainFrame);
		}
		{
			AQ_PROFILE_SCOPE("BuildCommandList");
			renderer_.BuildCommandList(mainFrame, *mainCmdList,
			                          Engine::Get().GetMainRenderTargetHandle(), renderW, renderH);
		}
#ifdef AQ_IMGUI
		if (imguiDrawData)
			mainCmdList->Enqueue<aq::rendering::ImGuiRenderCommand>(imguiDrawData);
#endif
		const auto sceneRT = Engine::Get().GetMainRenderTargetHandle();
		renderThread_.Submit(std::move(mainCmdList), renderer_.GetDisplayRTHandle(sceneRT),
		                    mainFrame.lighting, mainFrame.shadow);
	}
}
