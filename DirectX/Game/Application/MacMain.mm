#include "stdafx.h"
// macOS のエントリ。Win32 デスクトップは Main.cpp、UWP(Xbox 道A)は UWPMain.cpp が
// 担うため、それ以外の構成では空 TU になる(同一プロジェクトに 2 つのエントリを共存させないため)。
#if defined(AQ_PLATFORM_MAC)
#import <Cocoa/Cocoa.h>
#include "Application.h"
#include "Platform/Mac/PlatformMac.h"

int main(int argc, const char* argv[])
{
	// UNREFERENCED_PARAMETER は windows.h の定義なので Mac では使えない。
	(void)argc;
	(void)argv;

	@autoreleasepool
	{
		// NSWindow を作る前に NSApplication を用意する。run を呼ばず自前でループを
		// 回すため、run が内部で行う finishLaunching(アプリのアクティブ化)は手動で呼ぶ。
		// TODO(Mac実機): 要確認 — 公式ドキュメントは「run が finishLaunching を呼ぶ」と
		// 書いているだけで、自前ループ時に手動で呼ぶ手順は明記されていない。これが
		// 抜けているとウィンドウが前面に来ない/キー入力が来ないことがある。
		// TODO(Mac実機): 要確認 — メニューの「終了」/ Cmd+Q は NSApplication の terminate:
		// が直接プロセスを落とすため、Engine::Finalize を通らない。P4 でアプリデリゲートの
		// applicationShouldTerminate: を用意して RequestExit へ寄せるか判断する。
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		[NSApp finishLaunching];

		// Mac プラットフォーム実装。ウィンドウ/イベントループの寿命は main が持つ。
		aq::platform::PlatformMac platform;

		aq::StartupMark("main");
		aq::Engine::Create();
		aq::Engine& engineInstance = aq::Engine::Get();
		engineInstance.CreateApplication<app::Application>();

		aq::InitializeParameter initializeParameter;
		initializeParameter.platform = &platform;
		initializeParameter.screenWidth = 1280;
		initializeParameter.screenHeight = 720;
		initializeParameter.renderWidth = 1280;
		initializeParameter.renderHeight = 720;
		if (engineInstance.Initialize(initializeParameter)) {
			engineInstance.RunGame();
		}
		engineInstance.Finalize();
	}

	return 0;
}
#endif // AQ_PLATFORM_MAC
