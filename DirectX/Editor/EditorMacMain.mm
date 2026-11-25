#include "stdafx.h"
// エディタの macOS エントリ。Game/Application/MacMain.mm と同じ起動で GameModule を動かし、
// エディタの UI 層(EditorLayer)を AppHost へ登録する(EditorMain.cpp の Mac 版。設計書/エディタ/E10_Macのエディタ.md)。
// .app へ同梱しない開発用の起動なので、SetupBundleEnvironment は持たない。
// 作業ディレクトリは Game/(`cd Game && ../build/<preset>/bin/Debug/Editor.app/Contents/MacOS/Editor`)。
#if defined(AQ_PLATFORM_MAC)
#import <Cocoa/Cocoa.h>
#include "GameModule.h"
#include "Platform/Mac/PlatformMac.h"
#include "Core/AppHost.h"
#include "Level/LevelEditor.h"
#include "ConsoleLog.h"
#include "EditorLayer.h"
#include <cstdio>
#include <cstring>


int main(int argc, const char* argv[])
{
	// 作業ディレクトリに Assets が無いとアセットもシェーダも見つからないので、始める前に止める。
	{
		NSFileManager* fileManager = [NSFileManager defaultManager];
		BOOL isDirectory = NO;
		if (![fileManager fileExistsAtPath:@"Assets" isDirectory:&isDirectory] || !isDirectory) {
			std::fprintf(stderr, "Editor: 作業ディレクトリに Assets がありません。DirectX/Game で起動してください "
			                     "(cd DirectX/Game && ../build/macos-ninja-metal-editor/bin/Debug/Editor.app/Contents/MacOS/Editor)\n");
			return 1;
		}
	}

	int exitCode = 0;

	@autoreleasepool
	{
		// NSWindow を作る前に NSApplication を用意する(MacMain.mm と同じ。自前ループなので finishLaunching を手動で呼ぶ)。
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		[NSApp finishLaunching];

		// Mac プラットフォーム実装。ウィンドウ/イベントループの寿命は main が持つ。
		aq::platform::PlatformMac platform;
		// コンソールの中身。エンジンの初期化のログから溜めるので、最初の StartupMark より前に受け取りを始める。
		// EditorLayer が参照するので、EditorLayer より先に置く(後に壊れる)。
		editor::ConsoleLog consoleLog;
		consoleLog.Attach();
		// エディタの UI 層。AppHost は所有せず参照するだけなので、Engine の Finalize より長く生かす。
		editor::EditorLayer editorLayer(consoleLog);

		aq::StartupMark("main");
		aq::Engine::Create();
		aq::Engine& engineInstance = aq::Engine::Get();
		app::RegisterAppModules(engineInstance.GetAppModuleRegistry());

		aq::InitializeParameter initializeParameter;
		initializeParameter.platform = &platform;
		initializeParameter.screenWidth = 1280;
		initializeParameter.screenHeight = 720;
		initializeParameter.renderWidth = 1280;
		initializeParameter.renderHeight = 720;
		// 起動引数(-app= / -mode= / -editor-port=)。argv[0] はプログラム名として読み飛ばされる
		initializeParameter.launch = aq::LaunchOptions::Parse(argc, argv);
		// 初期化に失敗したら終了コード 1 を返す(起動した側が失敗を判別できるように)。
		if (engineInstance.Initialize(initializeParameter)) {
			// AppHost は Initialize の中で生成されるので、登録は成功の後(最初のフレームより前)に行う。
			engineInstance.GetAppHost()->SetEditorLayer(&editorLayer);
			// 編集モデルは Level エディタのパネルが持つものを使うが、Level エディタのウィンドウとメニューは出さない
			// (同じモデルを 2 つの UI が触ると、Level エディタの選択が宙に浮き、変更がコマンドを通らないため)
			if (aq::level::LevelEditorPanel* levelEditorPanel = engineInstance.GetAppHost()->GetLevelEditorPanel()) {
				levelEditorPanel->SetHostedByEditor(true);
			}
			// Play/Stop 自己テスト(-editor-selftest)。エディタだけの指示なので LaunchOptions には足さず、ここで直接見る
			for (int argIndex = 1; argIndex < argc; ++argIndex) {
				if (std::strcmp(argv[argIndex], "-editor-selftest") == 0) {
					editorLayer.RequestSelfTestOnLaunch();
					break;
				}
			}
			engineInstance.RunGame();
			// RequestExit で記録された終了コード(正常終了は 0、実行中の致命失敗は 2)。Release の前に読む。
			exitCode = engineInstance.GetExitCode();
		} else {
			exitCode = 1;
		}
		engineInstance.Finalize();
		aq::Engine::Release();
		// ワーカーのスレッドが止まった後に受け取りをやめる
		consoleLog.Detach();
	}

	// Engine もプラットフォーム(autoreleasepool 内のローカル)も壊れた後に畳む。
	// ここで初めてリーク報告が意味を持つ。
	aq::ShutdownMemory();
	return exitCode;
}
#endif // AQ_PLATFORM_MAC
