#pragma once
#include <windows.h>
#include <string>
#include "Platform/IPlatform.h"

namespace aq
{
	namespace platform
	{
		// Win32 デスクトップ向けプラットフォーム実装。
		// 旧 Engine が直接持っていたウィンドウ生成・メッセージループ・WndProc を移設したもの。
		// 道A(UWP) 移行後も、開発機での回帰確認用に維持する。
		class PlatformWin32 : public IPlatform
		{
		private:
			HINSTANCE hInstance_;
			int       nCmdShow_;
			HWND      hWnd_;

			/** ユーザーデータの書き込み先(末尾セパレータ付き)。初回要求時に解決してキャッシュする */
			std::string userDataDirectory_;
			bool        userDataDirectoryResolved_;

		public:
			PlatformWin32(HINSTANCE hInstance, int nCmdShow);
			~PlatformWin32() override;

		public:
			bool CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out) override;
			bool PumpEvents() override;
			const char* GetContentRoot() override;
			const char* GetUserDataDirectory() override;

		private:
			static LRESULT CALLBACK MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
		};
	}
}
