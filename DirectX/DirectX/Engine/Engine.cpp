#include "EnginePreCompile.h"
#include "Engine.h"
#include "Application.h"

namespace engine
{
	Engine* Engine::instance_ = nullptr;


	Engine::Engine()
		: hInstance_(nullptr)
		, hWnd_(nullptr)
		, driverType_(D3D_DRIVER_TYPE_NULL)
		, featureLevel_(D3D_FEATURE_LEVEL_11_0)
		, d3dDevice_(nullptr)
		, renderContext_()
		, deviceContext_(nullptr)
		, swapChain_(nullptr)
		, currentMainRenderTarget_(0)
		, mainRenderTargets_{}
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
		// ウィンドウ初期化
		if (!InitializeWindow(initializeParameter)) {
			return false;
		}
		// グラフィックAPI初期化
		if (!InitializeGraphicsAPI(initializeParameter)) {
			return false;
		}

		application_->Initialize();
		application_->Register();

		return true;
	}


	void Engine::Finalize()
	{
		mainRenderTargets_[0].Release();
		mainRenderTargets_[1].Release();
		if (swapChain_) {
			swapChain_->Release();
			swapChain_ = nullptr;
		}
		if (deviceContext_) {
			deviceContext_->ClearState();
			deviceContext_->Release();
			deviceContext_ = nullptr;
		}
		if (d3dDevice_) {
			d3dDevice_->Release();
			d3dDevice_ = nullptr;
		}

		if (application_) {
			application_->Finalize();
			delete application_;
			application_ = nullptr;
		}
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
				//更新。
				Update();
			}
		}
	}


	bool Engine::InitializeWindow(const InitializeParameter& initializeParameter)
	{
		EngineAssert(initializeParameter.screenHeight);
		EngineAssert(initializeParameter.screenWidth);

		screenHeight_ = initializeParameter.screenHeight;
		screenWidth_ = initializeParameter.screenWidth;
		WNDCLASSEX wc = {
			sizeof(WNDCLASSEX), CS_CLASSDC, MsgProc, 0L, 0L,
			GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
			TEXT("Application"), nullptr
		};
		RegisterClassEx(&wc);
		hWnd_ = CreateWindow(TEXT("Application"), TEXT("Application"),
			WS_OVERLAPPEDWINDOW, 0, 0, screenWidth_, screenHeight_,
			nullptr, nullptr, initializeParameter.hInstance, nullptr);

		ShowWindow(hWnd_, initializeParameter.nCmdShow);

		return hWnd_ != nullptr;
	}


	bool Engine::InitializeGraphicsAPI(const InitializeParameter& initializeParameter)
	{
		uint32_t createDeviceFlags = 0;
#ifdef _DEBUG
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
		D3D_DRIVER_TYPE driverTypes[] =
		{
			D3D_DRIVER_TYPE_HARDWARE,
			D3D_DRIVER_TYPE_WARP,
			D3D_DRIVER_TYPE_REFERENCE,
		};
		D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};
		uint32_t featureLevelsNum = ARRAYSIZE(featureLevels);

		renderHeight_ = initializeParameter.renderHeight;
		renderWidth_ = initializeParameter.renderWidth;
		// スワップチェイン生成
		DXGI_SWAP_CHAIN_DESC desc;
		memory::Clear(&desc, sizeof(desc));
		desc.BufferCount = 1;
		desc.BufferDesc.Width = renderWidth_;
		desc.BufferDesc.Height = renderHeight_;
		desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferDesc.RefreshRate.Numerator = 60;
		desc.BufferDesc.RefreshRate.Denominator = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.OutputWindow = hWnd_;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Windowed = TRUE;
		desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		desc.Flags = 0;
		//すべてのドライバタイプでスワップチェイン生成を試す
		HRESULT hr = E_FAIL;
		for (const auto driverType : driverTypes) {
			driverType_ = driverType;
			hr = D3D11CreateDeviceAndSwapChain(nullptr, driverType_, nullptr, createDeviceFlags, featureLevels, featureLevelsNum,
				D3D11_SDK_VERSION, &desc, &swapChain_, &d3dDevice_, &featureLevel_, &deviceContext_);
			if (SUCCEEDED(hr)) {
				//スワップチェインを作成できたのでループを抜ける。
				break;
			}
		}
		if (FAILED(hr)) {
			// スワップチェイン生成失敗
			return false;
		}
		
		// 書き込み先となるレンダリングターゲット生成
		ID3D11Texture2D* backBuffer = nullptr;
		hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
		if (FAILED(hr)) {
			return false;
		}
		DXGI_SAMPLE_DESC sampleDesc;
		sampleDesc.Count = 1;
		sampleDesc.Quality = 0;
		bool ret = mainRenderTargets_[0].Create(renderWidth_, renderHeight_, 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT, sampleDesc);
		ret = mainRenderTargets_[1].Create(renderWidth_, renderHeight_, 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT, sampleDesc);
		if (!ret) {
			return false;
		}

		// レンダリングコンテキスト初期化
		renderContext_.Initialize(deviceContext_);

		renderContext_.OMSetRenderTargets(1, &mainRenderTargets_[0]);

		// ビューポート設定
		renderContext_.RSSetViewport(0.0f, 0.0f, (FLOAT)renderWidth_, (FLOAT)renderHeight_);

		// ラスタライザ設定
		{
			D3D11_RASTERIZER_DESC desc;
			memory::Clear(&desc, sizeof(desc));
			desc.FillMode = D3D11_FILL_SOLID;
			desc.CullMode = D3D11_CULL_BACK;
			desc.DepthClipEnable = false;
			desc.MultisampleEnable = false;
			desc.DepthBiasClamp = 0;
			desc.SlopeScaledDepthBias = 0;

			ID3D11RasterizerState* state_;
			d3dDevice_->CreateRasterizerState(&desc, &state_);
			renderContext_.RSSetState(state_);
		}

		return true;
	}


	void Engine::Update()
	{
		application_->Update(renderContext_);
		CopyMainRenderTargetToBackBuffer();
		swapChain_->Present(0, 0);
	}


	void Engine::CopyMainRenderTargetToBackBuffer()
	{
		ID3D11Texture2D* backBuffer = nullptr;
		swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);

		deviceContext_->CopyResource(
			backBuffer,
			mainRenderTargets_[currentMainRenderTarget_].GetRenderTarget()
		);
		backBuffer->Release();
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