#include "stdafx.h"
// Android のエントリ。Win32 デスクトップは Main.cpp、UWP(Xbox 道A)は UWPMain.cpp、
// macOS は MacMain.mm が担うため、それ以外の構成では空 TU になる
// (同一プロジェクトに 2 つのエントリを共存させないため)。
#if defined(AQ_PLATFORM_ANDROID)
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include "Application.h"
#include "Platform/Android/PlatformAndroid.h"


namespace
{
	/**
	 * エンジンの起動から終了までを 1 つの関数に閉じ込める。
	 *
	 * android_main に直接書くと、プラットフォーム実装(ローカル変数)が android_main を
	 * 抜けるまで生き残り、リーク報告がそれを「未解放」として数えてしまう。
	 * ここで区切ることで、報告は platform の破棄より後に出せる。
	 */
	void RunEngine(android_app* app)
	{
		// ウィンドウ/イベントループの寿命はこの関数が持つ(Main.cpp の WinMain と同じ)。
		aq::platform::PlatformAndroid platform(app);

		// Win32 / Mac と違い、解像度をこちらから決められない。Engine へ渡す値が要るので、
		// 先にウィンドウの到着を待ってサイズを取る。CreateMainWindow は冪等なので、
		// この後 Engine::Initialize が内部で呼んでも同じウィンドウが返る。
		aq::graphics::NativeWindowHandle window;
		aq::platform::WindowDesc         desc;
		if (!platform.CreateMainWindow(desc, window))
		{
			aq::StartupMark("android_main exit (no window)");
			return;
		}

		ANativeWindow* nativeWindow = static_cast<ANativeWindow*>(window.handle);
		const int32_t  width        = ANativeWindow_getWidth(nativeWindow);
		const int32_t  height       = ANativeWindow_getHeight(nativeWindow);

		// アセットは APK 内にあり fopen できないので、ここで内部ストレージへ展開しておく。
		// 展開は GetContentRoot の初回呼び出しで走る。Engine 初期化の途中で数秒止まると
		// 何が起きているか分からなくなるため、起動直後に明示的に済ませて印を残す。
		if (platform.GetContentRoot() == nullptr)
		{
			aq::StartupMark("android_main exit (asset extraction failed)");
			return;
		}

		aq::Engine::Create();
		aq::Engine& engineInstance = aq::Engine::Get();
		engineInstance.CreateApplication<app::Application>();

		aq::InitializeParameter initializeParameter;
		initializeParameter.platform     = &platform;
		initializeParameter.screenWidth  = width;
		initializeParameter.screenHeight = height;
		initializeParameter.renderWidth  = width;
		initializeParameter.renderHeight = height;
		if (engineInstance.Initialize(initializeParameter)) {
			engineInstance.RunGame();
		}
		engineInstance.Finalize();
		aq::Engine::Release();
	}
}


extern "C" void android_main(android_app* app)
{
	aq::StartupMark("android_main");

	RunEngine(app);

	// Engine もプラットフォームも壊れた後に畳む。ここで初めてリーク報告が意味を持つ。
	aq::ShutdownMemory();
	aq::StartupMark("android_main exit");
}

#endif // AQ_PLATFORM_ANDROID
