#pragma once
#include <windows.h>
#include <cstddef>
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsDevice.h"
#include "Memory/MemoryManager.h"
#include "Rendering/RenderTargetHandle.h"
#include "Util/GameTimer.h"


namespace aq
{
	class IApplication;

	struct InitializeParameter
	{
		HINSTANCE hInstance;
		int32_t   screenWidth;
		int32_t   screenHeight;
		int32_t   renderWidth;
		int32_t   renderHeight;
		int32_t   nCmdShow;
		uint8_t   gameObjectPriortyMax;
		aq::memory::MemoryConfig memoryConfig; // アロケータサイズ設定 (デフォルト値あり)
	};

	class Engine
	{
	private:
		HINSTANCE hInstance_;
		HWND      hWnd_;

		aq::graphics::RenderContext renderContext_;

		uint32_t currentMainRenderTarget_;
		uint32_t screenWidth_;
		uint32_t screenHeight_;
		uint32_t renderWidth_;
		uint32_t renderHeight_;

		IApplication* application_;

		aq::util::GameTimer gameTimer_;


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

	public:
		inline int32_t GetRenderWidth()  const { return renderWidth_; }
		inline int32_t GetRenderHeight() const { return renderHeight_; }

		inline aq::util::GameTimer& GetTimer() { return gameTimer_; }

		// static ラッパー (Engine::Get() を省略して呼べる)
		static float GetDeltaTime() { return Get().gameTimer_.GetDeltaTime(); }
		static float GetTotalTime() { return Get().gameTimer_.GetTotalTime(); }
		static float GetFPS()       { return Get().gameTimer_.GetFPS(); }
		static void  SetFPSLimit(float fps) { Get().gameTimer_.SetFPSLimit(fps); }

		inline void ToggleMainRenderTarget() { currentMainRenderTarget_ ^= 1; }
		inline aq::graphics::IRenderTarget& GetMainRenderTarget()
		{
			return aq::graphics::GraphicsDevice::Get().GetMainRenderTarget(currentMainRenderTarget_);
		}
		/** 現在のメインRTへの RenderTargetHandle を返す。コマンド記録時に使う。 */
		inline aq::rendering::RenderTargetHandle GetMainRenderTargetHandle() const
		{
			return aq::rendering::RenderTargetHandle{ currentMainRenderTarget_ };
		}

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
		static void Create()
		{
			EngineAssert(instance_ == nullptr);
			instance_ = new Engine();
		}
		static Engine& Get()  { return *instance_; }
		static void Release()
		{
			if (instance_) {
				delete instance_;
				instance_ = nullptr;
			}
		}


	private:
		static LRESULT CALLBACK MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	};
}
