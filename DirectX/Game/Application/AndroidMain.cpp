#include "stdafx.h"
// Android のエントリ。Win32 デスクトップは Main.cpp、UWP(Xbox 道A)は UWPMain.cpp、
// macOS は MacMain.mm が担うため、それ以外の構成では空 TU になる
// (同一プロジェクトに 2 つのエントリを共存させないため)。
#if defined(AQ_PLATFORM_ANDROID)
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include "Platform/Android/PlatformAndroid.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/RenderContext.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include "Memory/MemoryManager.h"


namespace
{
	/**
	 * グラフィクスデバイスだけを立てて、クリア色を提示し続ける最小ループ。
	 *
	 * Engine::Initialize はシェーダやテクスチャの読み込みを伴うが、APK 内の assets は
	 * 通常のファイルパスでは開けないため、アセットを端末へ展開する仕組みが入るまでは
	 * ゲーム本体を起動できない。ここではその前段として、
	 * 「プラットフォーム層 → ANativeWindow → Vulkan のサーフェス/スワップチェーン/提示」
	 * までが実機で通ることだけを確認する。
	 *
	 * TODO(P2): アセット展開を入れたら、この関数を Main.cpp / MacMain.mm と同じ
	 *           Engine::Create → Initialize → RunGame → Finalize のブートへ置き換える。
	 */
	void RunClearScreen(aq::platform::PlatformAndroid& platform)
	{
		ANativeWindow* nativeWindow = platform.GetWindow();
		if (nativeWindow == nullptr)
		{
			return;
		}

		const uint32_t width  = static_cast<uint32_t>(ANativeWindow_getWidth(nativeWindow));
		const uint32_t height = static_cast<uint32_t>(ANativeWindow_getHeight(nativeWindow));

		aq::graphics::NativeWindowHandle window;
		window.handle = nativeWindow;

		aq::graphics::GraphicsDevice::Create<aq::graphics::VulkanGraphicsDeviceImpl>();
		if (!aq::graphics::GraphicsDevice::Get().Initialize(window, width, height))
		{
			aq::StartupMark("[android] GraphicsDevice::Initialize FAILED");
			aq::graphics::GraphicsDevice::Release();
			return;
		}
		aq::StartupMark("[android] graphics device ok");

		aq::graphics::RenderContext context;
		aq::graphics::GraphicsDevice::Get().SetupRenderContext(context);
		aq::graphics::GraphicsDevice::Get().SetupDefaultRenderState(context);

		// 見て分かる色にする(黒だと「何も出ていない」と区別が付かない)。
		float clearColor[4] = { 0.10f, 0.35f, 0.60f, 1.0f };

		uint32_t presentedFrames = 0;
		while (platform.PumpEvents())
		{
			// ウィンドウが無い間(バックグラウンド)は提示先が無いので描かない。
			if (!platform.IsRenderable())
			{
				continue;
			}

			aq::graphics::IRenderTarget& mainRT =
				aq::graphics::GraphicsDevice::Get().GetMainRenderTarget(0);

			context.OMSetRenderTargets(1, &mainRT);
			context.RSSetViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
			context.ClearRenderTargetView(0, clearColor);

			aq::graphics::GraphicsDevice::Get().CopyToBackBuffer(mainRT);
			aq::graphics::GraphicsDevice::Get().Present();

			// 提示が回り始めたことが logcat で分かるように、最初の数フレームだけ印を出す。
			if (presentedFrames < 3)
			{
				++presentedFrames;
				aq::StartupMarkf("[android] presented frame %u", presentedFrames);
			}
		}

		aq::graphics::GraphicsDevice::Get().WaitIdle();
		aq::graphics::GraphicsDevice::Get().Finalize();
		aq::graphics::GraphicsDevice::Release();
	}
}


extern "C" void android_main(android_app* app)
{
	aq::StartupMark("android_main");

	// ウィンドウ/イベントループの寿命は android_main が持つ(Main.cpp の WinMain と同じ)。
	aq::platform::PlatformAndroid platform(app);

	// ウィンドウ・グラフィクス初期化中の new/delete もエンジンアロケータ管理下に置く
	// (Engine::Initialize が先頭で行っているのと同じ理由)。
	aq::memory::MemoryConfig memoryConfig;
	aq::memory::MemoryManager::Initialize(memoryConfig);

	aq::graphics::NativeWindowHandle window;
	aq::platform::WindowDesc         desc;
	if (platform.CreateMainWindow(desc, window))
	{
		RunClearScreen(platform);
	}

	aq::memory::MemoryManager::Finalize();
	aq::StartupMark("android_main exit");
}

#endif // AQ_PLATFORM_ANDROID
