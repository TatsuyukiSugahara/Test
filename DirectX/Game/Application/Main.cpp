#include "stdafx.h"
// Win32 デスクトップのエントリ。UWP(Xbox 道A)では UWPMain.cpp が担うため、
// この TU は空になる(同一プロジェクトに 2 つのエントリを共存させないため)。
#if !defined(AQ_PLATFORM_UWP)
#include "Application.h"
#include "Platform/PlatformWin32.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// Win32 プラットフォーム実装。ウィンドウ/メッセージループの寿命は WinMain が持つ。
	// 道A(UWP) では PlatformUWP に差し替えるブートストラップになる。
	aq::platform::PlatformWin32 platform(hInstance, nCmdShow);

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

	return 0;
}
#endif // !AQ_PLATFORM_UWP
