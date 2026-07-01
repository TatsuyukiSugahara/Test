#include "aq.h"
#if defined(AQ_PLATFORM_UWP)
#include "Platform/PlatformUWP.h"
#include <windows.h>   // WideCharToMultiByte(UWP でも利用可)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Graphics.Display.h>

using namespace winrt;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::Graphics::Display;

namespace aq
{
	namespace platform
	{
		namespace
		{
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
		}


		PlatformUWP::~PlatformUWP()
		{
			if (window_)
			{
				window_.Closed(closedToken_);
			}
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
			// PLM: suspend 中は 128MB 以下に収める必要がある(Phase 4 で GPU/大バッファ解放)。
		}


		void PlatformUWP::OnResume()
		{
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
}
#endif // AQ_PLATFORM_UWP
