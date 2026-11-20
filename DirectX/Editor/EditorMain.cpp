#include "stdafx.h"
// エディタの Win32 エントリ。Game と同じ起動で GameModule を動かし、エディタの UI 層(EditorLayer)を AppHost へ登録する。
// プラットフォームの起動処理なので Win32 の型を使う(パネル・コマンドなどエディタ本体のコードには書かない)。
#if defined(AQ_PLATFORM_WIN32)
#include "GameModule.h"
#include "Platform/PlatformWin32.h"
#include "EditorLayer.h"
#include <cstring>

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);

	int exitCode = 0;

	// Win32 プラットフォーム実装。ウィンドウ/メッセージループの寿命は WinMain が持つ。
	// 道A(UWP) では PlatformUWP に差し替えるブートストラップになる。
	// スコープで囲むのは、リーク報告(ShutdownMemory)を platform の破棄より後に出すため。
	{
		aq::platform::PlatformWin32 platform(hInstance, nCmdShow);
		// エディタの UI 層。AppHost は所有せず参照するだけなので、Engine の Finalize より長く生かす。
		editor::EditorLayer editorLayer;

		aq::StartupMark("WinMain");
		aq::Engine::Create();
		aq::Engine& engineInstance = aq::Engine::Get();
		app::RegisterAppModules(engineInstance.GetAppModuleRegistry());

		aq::InitializeParameter initializeParameter;
		initializeParameter.platform = &platform;
		initializeParameter.screenWidth = 1280;
		initializeParameter.screenHeight = 720;
		initializeParameter.renderWidth = 1280;
		initializeParameter.renderHeight = 720;
		// 起動引数(-app= / -mode= / -editor-port= / -parent-hwnd=)。lpCmdLine はプログラム名を含まない
		initializeParameter.launch = aq::LaunchOptions::ParseCommandLine(lpCmdLine);
		// 初期化に失敗したら終了コード 1 を返す(エディタなど起動した側が失敗を判別できるように)。
		if (engineInstance.Initialize(initializeParameter)) {
			// AppHost は Initialize の中で生成されるので、登録は成功の後(最初のフレームより前)に行う。
			// フックは毎フレームの更新の中で呼ばれるだけなので、初期化中に登録されている必要はない。
			engineInstance.GetAppHost()->SetEditorLayer(&editorLayer);
			// Play/Stop 自己テスト(-editor-selftest)。エディタだけの指示なので LaunchOptions には足さず、ここで直接見る
			// (LaunchOptions は未知のキーとしてログに出すだけで無視する)
			if (lpCmdLine != nullptr && std::strstr(lpCmdLine, "-editor-selftest") != nullptr) {
				editorLayer.RequestSelfTestOnLaunch();
			}
			engineInstance.RunGame();
			// RequestExit で記録された終了コード(正常終了は 0、実行中の致命失敗は 2)。Release の前に読む。
			exitCode = engineInstance.GetExitCode();
		} else {
			exitCode = 1;
		}
		engineInstance.Finalize();
		aq::Engine::Release();
	}

	// Engine もプラットフォームも壊れた後に畳む。ここで初めてリーク報告が意味を持つ。
	aq::ShutdownMemory();
	return exitCode;
}
#endif // AQ_PLATFORM_WIN32
