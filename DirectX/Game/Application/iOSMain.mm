#include "stdafx.h"
// iOS のエントリ。Win32 デスクトップは Main.cpp、UWP(Xbox 道A)は UWPMain.cpp、
// macOS は MacMain.mm、Android は AndroidMain.cpp が担うため、それ以外の構成では
// 空 TU になる(同一プロジェクトに 2 つのエントリを共存させないため)。
//
// Game/CMakeLists.txt は if(NOT APPLE) で .mm を除外しているだけで、APPLE は iOS でも
// 真になる。よって MacMain.mm と本ファイルは iOS / macOS の双方でコンパイルされ、
// どちらが実体を持つかはガードマクロが決める(CMake 側の振り分けは不要)。
#if defined(AQ_PLATFORM_IOS)
#import <UIKit/UIKit.h>
#include "Application.h"
#include "Platform/iOS/PlatformiOS.h"

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
// ARC を有効にすると release 呼び出しがコンパイルエラーになるため、早期に落とす。
#if __has_feature(objc_arc)
#error "iOSMain.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


// UIKit のアプリケーションデリゲート。
//
// UIApplicationMain は戻ってこないので、MacMain.mm のように main の中で
// 「生成 → 実行 → 後始末」を並べる形は取れない。ブートストラップを
// application:didFinishLaunchingWithOptions: へ、後始末を
// applicationWillTerminate: へ分けて持つ(設計書/iOS移植設計.md §3.2)。
// プラットフォーム実装の寿命もここが持つ(Mac はスタックに置いていた)。
@interface AqAppDelegate : UIResponder <UIApplicationDelegate>
{
	aq::platform::PlatformiOS* platform_;
}
@end


@implementation AqAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
	(void)application;
	(void)launchOptions;

	aq::StartupMark("iOSMain");

	// UIApplicationMain が戻らないので、プラットフォーム実装はスタックに置けない。
	// 寿命はこのデリゲートが持ち、applicationWillTerminate: で壊す。
	platform_ = new aq::platform::PlatformiOS();

	// Win32 / Mac と違い、解像度をこちらから決められない。Engine へ渡す値が要るので、
	// 先にウィンドウの到着を待ってサイズを取る。CreateMainWindow は冪等なので、
	// この後 Engine::Initialize が内部で呼んでも同じウィンドウが返る。
	// (AndroidMain.cpp とまったく同じ理由・同じ順序。設計書 §3.5)
	aq::graphics::NativeWindowHandle window;
	aq::platform::WindowDesc         desc;
	if (!platform_->CreateMainWindow(desc, window))
	{
		aq::StartupMark("iOSMain exit (no window)");
		return YES;
	}

	// 画面サイズは OS が決めるので、desc ではなく実際に確保できた寸法を使う(§3.5)。
	const int32_t width  = platform_->GetDrawableWidth();
	const int32_t height = platform_->GetDrawableHeight();

	aq::Engine::Create();
	aq::Engine& engineInstance = aq::Engine::Get();
	engineInstance.CreateApplication<app::Application>();

	aq::InitializeParameter initializeParameter;
	initializeParameter.platform     = platform_;
	initializeParameter.screenWidth  = width;
	initializeParameter.screenHeight = height;
	initializeParameter.renderWidth  = width;
	initializeParameter.renderHeight = height;
	if (engineInstance.Initialize(initializeParameter)) {
		// **ここで Finalize を続けて呼んではいけない。**
		// RunGame は iOS では RunFrameLoop 経由で CADisplayLink を張って**即 return する**
		// (while (PumpEvents()) が成立しないため。設計書 §3.3)。MacMain.mm / AndroidMain.cpp の
		// 「RunGame の直後に Finalize」をそのまま写すと 1 フレームも回らずに終了する(§3.2)。
		// 後始末は applicationWillTerminate: が持つ。
		engineInstance.RunGame();
	}

	return YES;
}


- (void)applicationWillTerminate:(UIApplication*)application
{
	(void)application;

	// **このメソッドが走る保証は無い(P1 で確認済み)。**
	// `xcrun simctl terminate` では呼ばれず、iOS の通常の終了は
	// 「サスペンド → 予告なく kill」なのでどのみち通らない。
	// それでも構造上ほかに置き場所が無い(UIApplicationMain が戻らないので
	// main の末尾に相当する場所がこのコールバックしかない)ため、置き場所は変えない。
	// つまり「行儀よく畳めたときだけ通る経路」であり、ここでのリーク報告や
	// Finalize の実行を前提にした設計はしないこと。

	// まずフレーム駆動を止める。CADisplayLink が生きたまま Engine を壊すと、
	// 次の表示更新で解放済みのデバイスへ描きに行く。
	if (platform_ != nullptr)
	{
		platform_->StopFrameLoop();
	}

	// ウィンドウが取れずに Engine を作らないまま抜けた経路があるので、生成済みだけ畳む。
	if (aq::Engine::IsCreated())
	{
		aq::Engine::Get().Finalize();
		aq::Engine::Release();
	}

	delete platform_;
	platform_ = nullptr;

	// Engine もプラットフォームも壊れた後に畳む。ここで初めてリーク報告が意味を持つ
	// (4 つのエントリ共通の順序)。
	aq::ShutdownMemory();
	aq::StartupMark("iOSMain exit");
}

@end


int main(int argc, char* argv[])
{
	@autoreleasepool
	{
		// 第 4 引数でデリゲートのクラス名を渡す。Info.plist に UIApplicationDelegate を
		// 書く方式は取らない(このファイルだけ読めば起動経路が分かるようにするため)。
		return UIApplicationMain(argc, argv, nil, @"AqAppDelegate");
	}
}
#endif // AQ_PLATFORM_IOS
