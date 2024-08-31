#pragma once
#include "Graphics/RenderContext.h"

namespace engine
{
	class IApplication;

	struct InitializeParameter
	{
		HINSTANCE hInstance;
		int32_t screenWidth;
		int32_t screenHeight;
		int32_t renderWidth;
		int32_t renderHeight;
		int32_t nCmdShow;
		uint8_t gameObjectPriortyMax;
	};

	class Engine
	{
	private:
		HINSTANCE hInstance_;
		HWND hWnd_;
		D3D_DRIVER_TYPE driverType_;
		D3D_FEATURE_LEVEL featureLevel_;
		ID3D11Device* d3dDevice_;
		graphics::RenderContext renderContext_;
		ID3D11DeviceContext* deviceContext_;
		IDXGISwapChain* swapChain_;
		uint32_t currentMainRenderTarget_;
		graphics::RenderTarget mainRenderTargets_[2];

		uint32_t screenWidth_;
		uint32_t screenHeight_;
		uint32_t renderWidth_;
		uint32_t renderHeight_;

		IApplication* application_;


	private:
		Engine();
		~Engine();


	public:
		bool Initialize(const InitializeParameter& initializeParameter);
		void Finalize();
		void RunGame();


	private:
		bool InitializeWindow(const InitializeParameter& initializeParameter);
		bool InitializeGraphicsAPI(const InitializeParameter& initializeParameter);
		void Update();
		void CopyMainRenderTargetToBackBuffer();

	public:
		/** デバイス取得 */
		inline ID3D11Device* GetD3DDevice() const { return d3dDevice_; }
		/** デバイスコンテキスト取得 */
		inline ID3D11DeviceContext* GetD3DDeviceContext() const { return deviceContext_; }
		/**  */
		inline int32_t GetRenderWidth() const { return renderWidth_; }
		inline int32_t GetRenderHeight() const { return renderHeight_; }

		inline void ToggleMainRenderTarget() { currentMainRenderTarget_ ^= 1; }
		inline graphics::RenderTarget& GetMainRenderTarget() { return mainRenderTargets_[currentMainRenderTarget_]; }


	public:
		HWND GetHWND() { return hWnd_; }

	public:
		template <typename _Application>
		void CreateApplication()
		{
			EngineAssert(application_ == nullptr);
			application_ = new _Application();
		}

	private:
		static Engine* instance_;

	public:
		/** インスタンス生成 */
		static void Create()
		{
			EngineAssert(instance_ == nullptr);
			instance_ = new Engine();
		}
		/** インスタンス取得 */
		static Engine& Get()
		{
			return *instance_;
		}
		/** インスタンス解放 */
		static void Release()
		{
			if (instance_) {
				delete instance_;
				instance_ = nullptr;
			}
		}


	private:
		/** ウィンドウプロシージャ */
		static LRESULT CALLBACK MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	};
}