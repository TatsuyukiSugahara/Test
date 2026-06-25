#include "stdafx.h"
#include "Application.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	aq::Engine::Create();
	aq::Engine& engineInstance = aq::Engine::Get();
	engineInstance.CreateApplication<app::Application>();

	aq::InitializeParameter initializeParameter;
	initializeParameter.nCmdShow = nCmdShow;
	initializeParameter.hInstance = hInstance;
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
