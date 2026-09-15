#include "aq.h"
// macOS のエントリ。Windows では Main.cpp が担うため、
// それ以外の構成では空 TU になる。
#if defined(AQ_PLATFORM_MAC)
#import <Cocoa/Cocoa.h>
#include "Application.h"
#include "Platform/Mac/PlatformMac.h"

int main(int /*argc*/, const char* /*argv*/[])
{
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

	// Engine もプラットフォームも壊れた後に畳む。
	aq::ShutdownMemory();
	return 0;
}
#endif // AQ_PLATFORM_MAC
