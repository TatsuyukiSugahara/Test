#include "EnginePreCompile.h"
#include "Engine.h"
#include "Application.h"
#include "Graphics/D3D11/D3D11GraphicsDeviceImpl.h"


namespace engine
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
		if (!InitializeWindow(initializeParameter)) {
			return false;
		}
		if (!InitializeGraphicsAPI(initializeParameter)) {
			return false;
		}

		application_->Initialize();
		application_->Register();

		return true;
	}


	void Engine::Finalize()
	{
		if (application_) {
			application_->Finalize();
			delete application_;
			application_ = nullptr;
		}

		graphics::GraphicsDevice::Get().Finalize();
		graphics::GraphicsDevice::Release();
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
		hWnd_ = CreateWindow(
			TEXT("Application"), TEXT("Application"),
			WS_OVERLAPPEDWINDOW, 0, 0, screenWidth_, screenHeight_,
			nullptr, nullptr, initializeParameter.hInstance, nullptr
		);

		ShowWindow(hWnd_, initializeParameter.nCmdShow);
		return hWnd_ != nullptr;
	}


	bool Engine::InitializeGraphicsAPI(const InitializeParameter& initializeParameter)
	{
		renderWidth_  = initializeParameter.renderWidth;
		renderHeight_ = initializeParameter.renderHeight;

		// D3D11 実装を注入 (将来 D3D12 / Vulkan に替える場合はここだけ変える)
		graphics::GraphicsDevice::Create(
			std::make_unique<graphics::D3D11GraphicsDeviceImpl>()
		);

		if (!graphics::GraphicsDevice::Get().Initialize(hWnd_, renderWidth_, renderHeight_)) {
			return false;
		}

		graphics::GraphicsDevice::Get().SetupRenderContext(renderContext_);
		graphics::GraphicsDevice::Get().SetupDefaultRenderState(renderContext_);

		renderContext_.OMSetRenderTargets(
			1,
			&graphics::GraphicsDevice::Get().GetMainRenderTarget(0)
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
		application_->Update(renderContext_);
		CopyMainRenderTargetToBackBuffer();
		graphics::GraphicsDevice::Get().Present();
	}


	void Engine::CopyMainRenderTargetToBackBuffer()
	{
		graphics::RenderTarget& rt =
			graphics::GraphicsDevice::Get().GetMainRenderTarget(currentMainRenderTarget_);
		graphics::GraphicsDevice::Get().CopyToBackBuffer(rt);
	}


	LRESULT CALLBACK Engine::MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
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
