#include "aq.h"
#if defined(AQ_PLATFORM_UWP)
#include "Platform/PlatformUWP.h"
#include <windows.h>   // WideCharToMultiByte(UWP でも利用可)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>   // StorageFolder::Path() の consume 定義に必要
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.System.h>    // MemoryManager(アプリメモリ上限/使用量)
#include <winrt/Windows.ApplicationModel.Core.h>  // CoreApplication(Suspending/Resuming)
#include <thread>                    // hardware_concurrency
#include "Graphics/GraphicsDevice.h" // PLM: サスペンド時に GPU をアイドル化/Trim

using namespace winrt;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Graphics::Display;

namespace aq
{
	namespace platform
	{
		namespace
		{
			// 起動時の実機リソースプロファイル(CPU コア数 / アプリメモリ上限)をログに出す。
			// Xbox の App/Game 種別で割当が変わるため、PlatformBudget 調整の実測に使う。
			void LogSystemInfo()
			{
				SYSTEM_INFO si = {};
				::GetNativeSystemInfo(&si);
				char b[200];
				sprintf_s(b, "  [sysinfo] CPU: dwNumberOfProcessors=%u, hardware_concurrency=%u",
				          si.dwNumberOfProcessors, std::thread::hardware_concurrency());
				aq::StartupLog(b);
				try
				{
					const uint64_t limit = winrt::Windows::System::MemoryManager::AppMemoryUsageLimit();
					const uint64_t used  = winrt::Windows::System::MemoryManager::AppMemoryUsage();
					sprintf_s(b, "  [sysinfo] AppMemory: limit=%lluMB, usage=%lluMB",
					          static_cast<unsigned long long>(limit >> 20),
					          static_cast<unsigned long long>(used >> 20));
					aq::StartupLog(b);
				}
				catch (...)
				{
					aq::StartupLog("  [sysinfo] MemoryManager query failed");
				}
			}


			std::string ToUtf8(std::wstring_view text)
			{
				if (text.empty()) return std::string();
				const int len = ::WideCharToMultiByte(
					CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
					nullptr, 0, nullptr, nullptr);
				std::string out(static_cast<size_t>(len), '\0');
				::WideCharToMultiByte(
					CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
					out.data(), len, nullptr, nullptr);
				return out;
			}
		}


		PlatformUWP::PlatformUWP(CoreWindow const& window)
			: window_(window)
		{
			// ウィンドウの閉じ要求で終了フラグを立てる。ループは PumpEvents で抜ける。
			closedToken_ = window_.Closed(
				[this](CoreWindow const&, CoreWindowEventArgs const&)
				{
					exitRequested_ = true;
				});

			// PLM: サスペンド/レジューム。サスペンド前に GPU をアイドル化し、
			// D3D11 なら IDXGIDevice3::Trim() で未使用メモリを解放する(復帰後の描画破壊防止)。
			// Suspending は deferral を取得して処理完了まで OS の中断を待たせる。
			suspendingToken_ = CoreApplication::Suspending(
				[this](winrt::Windows::Foundation::IInspectable const&,
				       winrt::Windows::ApplicationModel::SuspendingEventArgs const& e)
				{
					auto deferral = e.SuspendingOperation().GetDeferral();
					OnSuspend();
					deferral.Complete();
				});
			resumingToken_ = CoreApplication::Resuming(
				[this](winrt::Windows::Foundation::IInspectable const&,
				       winrt::Windows::Foundation::IInspectable const&)
				{
					OnResume();
				});

			LogSystemInfo();
		}


		PlatformUWP::~PlatformUWP()
		{
			if (window_)
			{
				window_.Closed(closedToken_);
			}
			CoreApplication::Suspending(suspendingToken_);
			CoreApplication::Resuming(resumingToken_);
		}


		bool PlatformUWP::CreateMainWindow(const WindowDesc& /*desc*/, aq::graphics::NativeWindowHandle& out)
		{
			// UWP は CoreWindow が既に存在する。CreateSwapChainForCoreWindow に渡す
			// IUnknown* を格納する(ウィンドウ生成そのものは行わない)。
			out.handle = winrt::get_unknown(window_);
			return out.handle != nullptr;
		}


		bool PlatformUWP::PumpEvents()
		{
			// 保留イベントを処理して即戻る(ゲームループを回し続ける)。
			window_.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
			return !exitRequested_;
		}


		void PlatformUWP::OnSuspend()
		{
			aq::StartupLog("  [PLM] OnSuspend (GPU idle / trim)");
			// GPU をアイドル化し、可能なら未使用 GPU メモリを解放する。
			// (D3D12=WaitForGPU / D3D11=ClearState+Flush+IDXGIDevice3::Trim)
			aq::graphics::GraphicsDevice::Get().OnSuspend();
		}


		void PlatformUWP::OnResume()
		{
			aq::StartupLog("  [PLM] OnResume");
			aq::graphics::GraphicsDevice::Get().OnResume();
		}


		const char* PlatformUWP::GetContentRoot()
		{
			if (contentRoot_.empty())
			{
				// パッケージ install フォルダ(sandbox の読み取り基点)。
				const auto path = Package::Current().InstalledLocation().Path();
				contentRoot_ = ToUtf8(std::wstring_view(path));
			}
			return contentRoot_.c_str();
		}


		void PlatformUWP::GetPixelSize(uint32_t& outWidth, uint32_t& outHeight) const
		{
			const auto bounds = window_.Bounds();   // 論理サイズ(DIP)
			float scale = 1.0f;
			try
			{
				scale = static_cast<float>(
					DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel());
			}
			catch (...)
			{
				scale = 1.0f;
			}
			outWidth  = static_cast<uint32_t>(bounds.Width  * scale + 0.5f);
			outHeight = static_cast<uint32_t>(bounds.Height * scale + 0.5f);
			if (outWidth  == 0) outWidth  = 1280;
			if (outHeight == 0) outHeight = 720;
		}
	}


	// 起動診断ログ。LocalState\startup.log に追記する(Device Portal から取得可能)。
	void StartupLog(const char* msg)
	{
		static std::mutex mtx;
		std::lock_guard<std::mutex> lk(mtx);
		static std::string path;
		if (path.empty())
		{
			try
			{
				const auto p = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
				path = winrt::to_string(p) + "\\startup.log";
			}
			catch (...) { return; }
		}
		FILE* fp = nullptr;
		if (fopen_s(&fp, path.c_str(), "a") == 0 && fp)
		{
			fprintf(fp, "%s\n", msg ? msg : "");
			fclose(fp);
		}
	}
}
#endif // AQ_PLATFORM_UWP
