#include "Utility.h"
#include "Application.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
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