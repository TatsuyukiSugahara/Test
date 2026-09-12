#pragma once
#include <cstddef>
#include <cstdint>
#if defined(AQ_PLATFORM_WIN32)
#include <windows.h>
#endif
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/GraphicsTypes.h"
#include "Memory/MemoryManager.h"
#include "Rendering/RenderTargetHandle.h"
#include "Util/GameTimer.h"
#include <future>


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

		/** サウンド初期化(XAudio2)を別スレッドで走らせた結果。EnsureSoundInitialized() で合流する */
		std::future<bool> soundInitFuture_;
		bool              soundInitialized_ = false;
		/** メインスレッドで CoInitializeEx(MTA) したか(プロセス寿命で保持し Finalize で解放) */
		bool              comInitialized_   = false;


	private:
		Engine();
		~Engine();


	public:
		bool Initialize(const InitializeParameter& initializeParameter);
		void Finalize();
		void RunGame();

		/**
		 * 別スレッドで進めているサウンド初期化の完了を待ち、成否を返す(2 回目以降は即返る)。
		 * SoundEngine を最初に使う直前に呼ぶ。Initialize() 内でも application 初期化後に必ず合流する。
		 */
		bool EnsureSoundInitialized();


	private:
		bool InitializeWindow(const InitializeParameter& initializeParameter);
		bool InitializeGraphicsAPI(const InitializeParameter& initializeParameter);
		void Update();

	public:
		inline int32_t GetRenderWidth()  const { return renderWidth_; }
		inline int32_t GetRenderHeight() const { return renderHeight_; }

		// ウィンドウ(クライアント領域)のサイズ。レンダー解像度とは別で、
		// ImGui の DisplaySize のようにウィンドウ座標系で扱うものが参照する。
		inline int32_t GetScreenWidth()  const { return static_cast<int32_t>(screenWidth_);  }
		inline int32_t GetScreenHeight() const { return static_cast<int32_t>(screenHeight_); }

		inline aq::util::GameTimer& GetTimer() { return gameTimer_; }

		// アセット読み込みの基点パス。Win32 は nullptr(=Resource 側の従来探索に委ねる)、
		// UWP はパッケージ install フォルダを返す。platform_ 未設定時も nullptr。
		const char* GetContentRoot() const;

		/**
		 * セーブデータなど、アプリが書き込んでよいディレクトリ (末尾セパレータ付き)。
		 * 用意できないプラットフォームでは nullptr。詳細は IPlatform::GetUserDataDirectory。
		 */
		const char* GetUserDataDirectory() const;

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
		/**
		 * プラットフォーム非依存のメインウィンドウハンドル。
		 * Win32: HWND / UWP: CoreWindow^ / Mac: CAMetalLayer* を void* として保持する。
		 */
		inline aq::graphics::NativeWindowHandle GetNativeWindowHandle() const { return window_; }

#if defined(AQ_PLATFORM_WIN32)
	public:
		// Win32 専用。DirectInput / ImGui_ImplWin32 など、まだ HWND を直接要求する
		// サブシステム向け。これらが GameInput 等に抽象化されたら撤去する最後の Win32 接合点。
		inline HWND GetHWND() const { return static_cast<HWND>(GetNativeWindowHandle().handle); }
#endif // AQ_PLATFORM_WIN32

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
