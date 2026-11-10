#include "aq.h"
// macOS のエントリ。Windows では Main.cpp が担うため、
// それ以外の構成では空 TU になる。
#if defined(AQ_PLATFORM_MAC)
#import <Cocoa/Cocoa.h>
#include "SampleModule.h"
#include "Platform/Mac/PlatformMac.h"

int main(int argc, const char* argv[])
{
	int exitCode = 0;

	@autoreleasepool
	{
		// NSWindow を作る前に NSApplication を用意する。run を呼ばず自前でループを
		// 回すため、run が内部で行う finishLaunching(アプリのアクティブ化)は手動で呼ぶ。
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		[NSApp finishLaunching];

		aq::platform::PlatformMac platform;

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
		// 起動引数(-app= / -mode= / -editor-port=)。argv[0] はプログラム名として読み飛ばされる
		initializeParameter.launch = aq::LaunchOptions::Parse(argc, argv);

		// 初期化に失敗したら終了コード 1 を返す(起動した側が失敗を判別できるように。Main.cpp と同じ)。
		if (engineInstance.Initialize(initializeParameter)) {
			engineInstance.RunGame();
			// RequestExit で記録された終了コード(正常終了は 0、実行中の致命失敗は 2)。Release の前に読む。
			exitCode = engineInstance.GetExitCode();
		} else {
			exitCode = 1;
		}
		engineInstance.Finalize();
		aq::Engine::Release();
	}

	// Engine もプラットフォームも壊れた後に畳む。
	aq::ShutdownMemory();
	return exitCode;
}
#endif // AQ_PLATFORM_MAC
