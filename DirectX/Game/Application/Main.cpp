#include "stdafx.h"
#include "Utility.h"
#include "Application.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	engine::Engine::Create();
	engine::Engine& engine = engine::Engine::Get();
	engine.CreateApplication<app::Application>();

	engine::InitializeParameter initializeParameter;
	initializeParameter.nCmdShow = nCmdShow;
	initializeParameter.hInstance = hInstance;
	initializeParameter.screenWidth = 1280;
	initializeParameter.screenHeight = 720;
	initializeParameter.renderWidth = 1280;
	initializeParameter.renderHeight = 720;
	if (engine.Initialize(initializeParameter)) {
		engine.RunGame();
	}
	engine.Finalize();

	return 0;
}
