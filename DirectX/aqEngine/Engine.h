#pragma once
#include <cstddef>
#include <cstdint>
#if !defined(AQ_PLATFORM_UWP)
#include <windows.h>
#endif
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/GraphicsTypes.h"
#include "Memory/MemoryManager.h"
#include "Rendering/RenderTargetHandle.h"
#include "Util/GameTimer.h"


namespace aq
{
	class IApplication;
	namespace platform { class IPlatform; }

	struct InitializeParameter
	{
		aq::platform::IPlatform* platform;     // ウィンドウ/ループ/ライフサイクルの実装（呼び出し側所有）
		int32_t   screenWidth;
		int32_t   screenHeight;
		int32_t   renderWidth;
		int32_t   renderHeight;
		uint8_t   gameObjectPriortyMax;
		aq::memory::MemoryConfig memoryConfig; // アロケータサイズ設定 (デフォルト値あり)
	};

	class Engine
	{
	private:
		aq::platform::IPlatform*           platform_;
		aq::graphics::NativeWindowHandle   window_;

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

		// アセット読み込みの基点パス。Win32 は nullptr(=Resource 側の従来探索に委ねる)、
		// UWP はパッケージ install フォルダを返す。platform_ 未設定時も nullptr。
		const char* GetContentRoot() const;

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

#if !defined(AQ_PLATFORM_UWP)
	public:
		// Win32 専用。DirectInput / ImGui_ImplWin32 など、まだ HWND を直接要求する
		// サブシステム向け。これらが GameInput 等に抽象化されたら撤去する最後の Win32 接合点。
		HWND GetHWND() const { return static_cast<HWND>(window_.handle); }
#endif

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
	};
}
