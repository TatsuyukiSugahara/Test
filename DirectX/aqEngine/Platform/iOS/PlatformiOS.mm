#include "aq.h"
// iOS(UIKit)専用実装。他構成では本体をガードして空 TU にする
// (Win32 は PlatformWin32.cpp、UWP は PlatformUWP.cpp、macOS は PlatformMac.mm が代替)。
#if defined(AQ_PLATFORM_IOS)
#include "Platform/iOS/PlatformiOS.h"

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
// ARC を有効にすると release 呼び出しがコンパイルエラーになるため、早期に落とす。
#if __has_feature(objc_arc)
#error "PlatformiOS.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace platform
	{
		// UIKit オブジェクト群の実体。
		// TODO(P1): UIWindow / ルート UIViewController / AqMetalView(+layerClass を
		// CAMetalLayer へ override したもの)/ CAMetalLayer をここへ持たせる。
		struct iOSWindowObjects
		{
		};


		/**
		 * iOS のプラットフォーム実装(P0 は骨格)
		 */
		PlatformiOS::PlatformiOS()
			: objects_(nullptr)
		{
		}


		PlatformiOS::~PlatformiOS()
		{
			delete objects_;
			objects_ = nullptr;
		}


		bool PlatformiOS::CreateMainWindow(const WindowDesc& /*desc*/, aq::graphics::NativeWindowHandle& /*out*/)
		{
			// TODO(P1): UIWindow とルート UIViewController、CAMetalLayer をホストする
			// AqMetalView を生成し、out へ CAMetalLayer* を返す(設計書/iOS移植設計.md §3.1)。
			// 呼べるのは UIApplicationDelegate のコールバック以降に限られる。
			return false;
		}


		bool PlatformiOS::PumpEvents()
		{
			// TODO(P1): イベントの配送は UIKit の run loop が行うので、ここは
			// 「終了要求フラグを見るだけ」になる(Win32 / Mac のようにイベントを
			// 汲み出す処理は持たない)。
			return false;
		}


		const char* PlatformiOS::GetContentRoot()
		{
			// TODO(P2): [[NSBundle mainBundle] bundlePath] を返す。iOS のバンドルは
			// Contents/ 階層を持たず、リソースがバンドル直下に並ぶ(設計書 §2.3 / §7.3)。
			return nullptr;
		}


		const char* PlatformiOS::GetUserDataDirectory()
		{
			// TODO(P2): NSDocumentDirectory を末尾セパレータ付きで返す。アプリの
			// コンテナ自体が既にアプリ専用なので、アプリ名のサブフォルダは作らない
			// (IPlatform::GetUserDataDirectory のコメントが iOS を名指しで決めている)。
			return nullptr;
		}
	}
}
#endif // AQ_PLATFORM_IOS
