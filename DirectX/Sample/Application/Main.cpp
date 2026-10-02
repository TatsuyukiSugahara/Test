#include "aq.h"
// Windows デスクトップのエントリ。Mac では MacMain.mm が担うため、
// それ以外の構成では空 TU になる(同一プロジェクトに 2 つのエントリを共存させないため)。
#if defined(AQ_PLATFORM_WIN32)
#include "SampleModule.h"
#include "Platform/PlatformWin32.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);

	int exitCode = 0;

	// スコープで囲むのは、リーク報告(ShutdownMemory)を platform の破棄より後に出すため。
	{
		aq::platform::PlatformWin32 platform(hInstance, nCmdShow);

		aq::Engine::Create();
		aq::Engine& engineInstance = aq::Engine::Get();
		sample::RegisterAppModules(engineInstance.GetAppModuleRegistry());

		aq::InitializeParameter initializeParameter;
		initializeParameter.platform     = &platform;
		initializeParameter.screenWidth  = 1280;
		initializeParameter.screenHeight = 720;
		initializeParameter.renderWidth  = 1280;
		initializeParameter.renderHeight = 720;
		// "Assets/..." をこのプロジェクトの Sample/Assets/ へ向ける。
		initializeParameter.gameRootName = "Sample";
		// 起動引数(-app= / -mode= / -editor-port= / -parent-hwnd=)。lpCmdLine はプログラム名を含まない
		initializeParameter.launch = aq::LaunchOptions::ParseCommandLine(lpCmdLine);

		// 初期化に失敗したら終了コード 1 を返す(エディタなど起動した側が失敗を判別できるように)。
		if (engineInstance.Initialize(initializeParameter)) {
			engineInstance.RunGame();
		} else {
			exitCode = 1;
		}
		engineInstance.Finalize();
		aq::Engine::Release();
	}

	aq::ShutdownMemory();
	return exitCode;
}
#endif // AQ_PLATFORM_WIN32
