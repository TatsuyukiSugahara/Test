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
			aq::StartupLog("=== Run begin ===");
			try
			{
				CoreWindow window = CoreWindow::GetForCurrentThread();
				window.Activate();
				aq::StartupLog("window activated");

				platform_ = std::make_unique<aq::platform::PlatformUWP>(window);
				aq::StartupLog("platform created");

				aq::Engine::Create();
				aq::Engine& engine = aq::Engine::Get();
				engine.CreateApplication<app::Application>();
				aq::StartupLog("engine + application created");

				aq::InitializeParameter ip;
				ip.platform = platform_.get();

				uint32_t w = 0, h = 0;
				platform_->GetPixelSize(w, h);
				{
					char buf[128];
					sprintf_s(buf, "pixel size = %u x %u", w, h);
					aq::StartupLog(buf);
				}
				ip.screenWidth  = static_cast<int32_t>(w);
				ip.screenHeight = static_cast<int32_t>(h);
				ip.renderWidth  = static_cast<int32_t>(w);
				ip.renderHeight = static_cast<int32_t>(h);

				aq::StartupLog("engine.Initialize() begin");
				if (engine.Initialize(ip))
				{
					aq::StartupLog("engine.Initialize() done -> RunGame()");
					engine.RunGame();   // while (platform_->PumpEvents()) Update();
					aq::StartupLog("RunGame() returned");
				}
				else
				{
					aq::StartupLog("engine.Initialize() returned FALSE");
				}
				engine.Finalize();
				aq::StartupLog("=== Run end ===");
			}
			catch (winrt::hresult_error const& e)
			{
				char buf[256];
				sprintf_s(buf, "!! hresult_error 0x%08X: %ls",
				          static_cast<unsigned>(e.code()), e.message().c_str());
				aq::StartupLog(buf);
				throw;
			}
			catch (std::exception const& e)
			{
				char buf[256];
				sprintf_s(buf, "!! std::exception: %s", e.what());
				aq::StartupLog(buf);
				throw;
			}
			catch (...)
			{
				aq::StartupLog("!! unknown exception");
				throw;
			}
		}

		void Uninitialize()
		{
		}
	};
}


int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	// CoreApplication は MTA からのアクセスが必要(未初期化だと hresult_wrong_thread)。
	// winrt::init_apartment() は既定で multi_threaded(MTA)。
	winrt::init_apartment();
	CoreApplication::Run(make<App>());
	return 0;
}
#endif // AQ_PLATFORM_UWP
