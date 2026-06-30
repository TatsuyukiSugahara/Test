#pragma once
#include <cstdint>
#include "Graphics/GraphicsTypes.h"

namespace aq
{
	namespace platform
	{
		// メインウィンドウ生成の要求パラメータ。
		// Win32 では通常ウィンドウ、UWP では CoreWindow のサイズヒントとして使う。
		struct WindowDesc
		{
			int32_t     width  = 1280;
			int32_t     height = 720;
			const char* title  = "Application";
		};

		// プラットフォーム抽象。
		// ウィンドウ生成・イベントループ・ライフサイクル・コンテンツ基点を隠蔽し、
		// Engine から Win32 / UWP / GDK の差分を切り離す。
		// 実装: PlatformWin32(現行 / 道A の回帰確認用), PlatformUWP(道A 本命, 将来),
		//       PlatformGDK(道B, 将来)。
		class IPlatform
		{
		public:
			virtual ~IPlatform() = default;

			// メインウィンドウを生成し、プラットフォーム非依存ハンドルを out に返す。
			// Win32: HWND、UWP: CoreWindow^ を void* として格納する。
			virtual bool CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out) = 0;

			// 保留中のイベントを処理する。終了要求を受けたら false を返す。
			virtual bool PumpEvents() = 0;

			// PLM ライフサイクル。UWP では suspend 中にメモリを 128MB 以下へ落とす起点。
			// Win32 では未使用（既定実装は何もしない）。
			virtual void OnSuspend() {}
			virtual void OnResume()  {}

			// アセット読み込みの基点パス。
			// Win32: ソースツリー / 実行ディレクトリ、UWP: パッケージ install フォルダ。
			virtual const char* GetContentRoot() = 0;
		};
	}
}
