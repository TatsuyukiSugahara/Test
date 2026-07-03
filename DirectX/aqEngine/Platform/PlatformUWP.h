#pragma once
// UWP(Xbox 道A)向けプラットフォーム実装。
// デスクトップ構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_UWP)
#include <string>
#include <cstdint>
#include <winrt/Windows.UI.Core.h>
#include "Platform/IPlatform.h"

namespace aq
{
	namespace platform
	{
		// CoreApplication/CoreWindow モデル上で IPlatform を実装する。
		// ウィンドウは UWP ランタイムが先に生成する(IFrameworkView::SetWindow)ため、
		// 本クラスは「既にある CoreWindow」を受け取ってラップするだけ。
		// イベントは Dispatcher でポンプし、Closed で終了要求を立てる。
		class PlatformUWP : public IPlatform
		{
		private:
			winrt::Windows::UI::Core::CoreWindow window_;
			winrt::event_token                   closedToken_{};
			winrt::event_token                   suspendingToken_{};   // PLM
			winrt::event_token                   resumingToken_{};
			std::string                          contentRoot_;
			bool                                 exitRequested_ = false;

		public:
			explicit PlatformUWP(winrt::Windows::UI::Core::CoreWindow const& window);
			~PlatformUWP() override;

		public:
			bool CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out) override;
			bool PumpEvents() override;
			void OnSuspend() override;
			void OnResume()  override;
			const char* GetContentRoot() override;

			// アプリ終了を要求する(PLM の閉じ要求などから呼ぶ)。
			void RequestExit() { exitRequested_ = true; }

			// CoreWindow の物理ピクセルサイズを返す(スワップチェーン初期サイズ用)。
			void GetPixelSize(uint32_t& outWidth, uint32_t& outHeight) const;
		};
	}
}
#endif // AQ_PLATFORM_UWP
