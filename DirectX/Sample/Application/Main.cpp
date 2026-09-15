#include "aq.h"
// Windows デスクトップのエントリ。Mac では MacMain.mm が担うため、
// それ以外の構成では空 TU になる(同一プロジェクトに 2 つのエントリを共存させないため)。
#if defined(AQ_PLATFORM_WIN32)
#include "Application.h"
#include "Platform/PlatformWin32.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// スコープで囲むのは、リーク報告(ShutdownMemory)を platform の破棄より後に出すため。
	{
		aq::platform::PlatformWin32 platform(hInstance, nCmdShow);

		aq::Engine::Create();
		aq::Engine& engineInstance = aq::Engine::Get();
		engineInstance.CreateApplication<sample::Application>();

		aq::InitializeParameter initializeParameter;
		initializeParameter.platform     = &platform;
		initializeParameter.screenWidth  = 1280;
		initializeParameter.screenHeight = 720;
		initializeParameter.renderWidth  = 1280;
		initializeParameter.renderHeight = 720;
		// "Assets/..." をこのプロジェクトの Sample/Assets/ へ向ける。
		initializeParameter.gameRootName = "Sample";

		if (engineInstance.Initialize(initializeParameter)) {
			engineInstance.RunGame();
		}
		engineInstance.Finalize();
		aq::Engine::Release();
	}

	aq::ShutdownMemory();
	return 0;
}
#endif // AQ_PLATFORM_WIN32
