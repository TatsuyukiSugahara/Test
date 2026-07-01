#include "stdafx.h"
// UWP(Xbox 道A)のエントリ。デスクトップ構成では空 TU(Win32 は Main.cpp が担う)。
#if defined(AQ_PLATFORM_UWP)
#include <memory>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include "Application.h"
#include "Engine.h"
#include "Platform/PlatformUWP.h"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;

namespace
{
	// IFrameworkView / IFrameworkViewSource を 1 クラスで実装する(標準的な最小構成)。
	// ゲームループは Run() の中で回す。Win32 の WinMain と同じブート手順を
	// PlatformUWP 経由で実行する。
	struct App : implements<App, IFrameworkViewSource, IFrameworkView>
	{
		std::unique_ptr<aq::platform::PlatformUWP> platform_;

		// IFrameworkViewSource
		IFrameworkView CreateView()
		{
			return *this;
		}

		// IFrameworkView
		void Initialize(CoreApplicationView const&)
		{
		}

		void SetWindow(CoreWindow const&)
		{
		}

		void Load(hstring const&)
		{
		}

		void Run()
		{
			CoreWindow window = CoreWindow::GetForCurrentThread();
			window.Activate();

			platform_ = std::make_unique<aq::platform::PlatformUWP>(window);

			aq::Engine::Create();
			aq::Engine& engine = aq::Engine::Get();
			engine.CreateApplication<app::Application>();

			aq::InitializeParameter ip;
			ip.platform = platform_.get();

			uint32_t w = 0, h = 0;
			platform_->GetPixelSize(w, h);
			ip.screenWidth  = static_cast<int32_t>(w);
			ip.screenHeight = static_cast<int32_t>(h);
			ip.renderWidth  = static_cast<int32_t>(w);
			ip.renderHeight = static_cast<int32_t>(h);

			if (engine.Initialize(ip))
			{
				engine.RunGame();   // while (platform_->PumpEvents()) Update();
			}
			engine.Finalize();
		}

		void Uninitialize()
		{
		}
	};
}


int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	CoreApplication::Run(make<App>());
	return 0;
}
#endif // AQ_PLATFORM_UWP
