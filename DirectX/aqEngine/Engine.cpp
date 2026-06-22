#include "aq.h"
#include "Engine.h"
#include "Core/IApplication.h"
#include "Util/ThreadPool.h"
#include "Physics/PhysicsBackend.h"
#ifdef ENGINE_GRAPHICS_D3D11
#include "Graphics/D3D11/D3D11GraphicsDeviceImpl.h"
#endif
#ifdef AQ_IMGUI
#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
// header 内で #if 0 されているため、使用する .cpp で前方宣言が必要
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif


namespace aq
{
	Engine* Engine::instance_ = nullptr;


	Engine::Engine()
		: hInstance_(nullptr)
		, hWnd_(nullptr)
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
		// メモリマネージャを最初に初期化することで、ウィンドウ・グラフィクス初期化中の
		// new/delete もエンジンアロケータ管理下に置く。
		aq::memory::MemoryManager::Initialize(initializeParameter.memoryConfig);

		// Bullet allocator hook は MemoryManager 直後、かつ Bullet 型が一切生成される前に設定する。
		aq::physics::PhysicsWorld::InstallAllocatorHook();

		if (!InitializeWindow(initializeParameter)) {
			return false;
		}
		if (!InitializeGraphicsAPI(initializeParameter)) {
			return false;
		}
		aq::util::ThreadPool::Initialize();

		if (!application_->Initialize(renderContext_)) {
			return false;
		}
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

		aq::graphics::GraphicsDevice::Get().Finalize();
		aq::graphics::GraphicsDevice::Release();

		aq::util::ThreadPool::Finalize();
		aq::memory::MemoryManager::Finalize();
	}


	void Engine::RunGame()
	{
		MSG msg = { 0 };
		while (WM_QUIT != msg.message)
		{
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				Update();
			}
		}
	}


	bool Engine::InitializeWindow(const InitializeParameter& initializeParameter)
	{
		EngineAssert(initializeParameter.screenHeight);
		EngineAssert(initializeParameter.screenWidth);

		screenHeight_ = initializeParameter.screenHeight;
		screenWidth_  = initializeParameter.screenWidth;

		WNDCLASSEX wc = {
			sizeof(WNDCLASSEX), CS_CLASSDC, MsgProc, 0L, 0L,
			GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
			TEXT("Application"), nullptr
		};
		RegisterClassEx(&wc);
		RECT rc = { 0, 0, static_cast<LONG>(screenWidth_), static_cast<LONG>(screenHeight_) };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
		hWnd_ = CreateWindow(
			TEXT("Application"), TEXT("Application"),
			WS_OVERLAPPEDWINDOW, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
			nullptr, nullptr, initializeParameter.hInstance, nullptr
		);

		ShowWindow(hWnd_, initializeParameter.nCmdShow);
		return hWnd_ != nullptr;
	}


	bool Engine::InitializeGraphicsAPI(const InitializeParameter& initializeParameter)
	{
		renderWidth_  = initializeParameter.renderWidth;
		renderHeight_ = initializeParameter.renderHeight;

		// 選択された API の実装を注入 (将来 D3D12 / Vulkan に替える場合は define を変えてここを追加する)
#ifdef ENGINE_GRAPHICS_D3D11
		aq::graphics::GraphicsDevice::Create<aq::graphics::D3D11GraphicsDeviceImpl>();
#endif // ENGINE_GRAPHICS_D3D11

		if (!aq::graphics::GraphicsDevice::Get().Initialize({ hWnd_ }, renderWidth_, renderHeight_)) {
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
		application_->Update();
		// FlushRender() はレンダースレッドがコマンドリストの実行・RT コピー・Present を
		// 完了するまで待機する。描画に関わるすべての D3D11 コンテキスト呼び出しは
		// レンダースレッド側に集約され、メインスレッドは Submit() 以降コンテキストに触れない。
		application_->FlushRender();
		aq::memory::MemoryManager::Get().ResetStackAllocator();
	}


	LRESULT CALLBACK Engine::MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
#ifdef AQ_IMGUI
		if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
			return true;
#endif

		PAINTSTRUCT ps;
		HDC hdc;

		switch (msg)
		{
			case WM_PAINT:
			{
				hdc = BeginPaint(hWnd, &ps);
				EndPaint(hWnd, &ps);
				break;
			}
			case WM_DESTROY:
			{
				PostQuitMessage(0);
				break;
			}
			default:
			{
				return DefWindowProc(hWnd, msg, wParam, lParam);
			}
		}
		return 0;
	}
}
