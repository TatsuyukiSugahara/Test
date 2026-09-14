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
#include "Platform/iOS/PlatformiOS.h"
#include "Graphics/Metal/MetalGraphicsDeviceImpl.h"

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
//
// TODO(P2): この足場を Engine::Create → CreateApplication → Engine::Initialize →
//   Engine::RunGame → Engine::Finalize → Engine::Release のブートへ置き換える。
//   アセットをバンドルへ入れる(package_app.cmake の iOS 分岐)のが P2 なので、
//   それまでゲーム本体は起動できない(Android P1 と同じ段取り)。
@interface AqAppDelegate : UIResponder <UIApplicationDelegate>
{
	aq::platform::PlatformiOS* platform_;
	aq::graphics::RenderContext renderContext_;
	uint32_t                    presentedFrames_;
	bool                        graphicsReady_;
}
@end


@implementation AqAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
	(void)application;
	(void)launchOptions;

	aq::StartupMark("iOSMain");

	platform_        = nullptr;
	presentedFrames_ = 0;
	graphicsReady_   = false;

	// ウィンドウ・グラフィクス初期化中の new/delete もエンジンアロケータ管理下に置く。
	// 通常は Engine::Initialize が先頭で行うが、P1 では Engine を起動しないので
	// 誰も初期化してくれない(AndroidMain.cpp の P1 版と同じ理由)。
	aq::memory::MemoryConfig memoryConfig;
	aq::memory::MemoryManager::Initialize(memoryConfig);

	// UIApplicationMain が戻らないので、プラットフォーム実装はスタックに置けない。
	// 寿命はこのデリゲートが持ち、applicationWillTerminate: で壊す。
	platform_ = new aq::platform::PlatformiOS();

	aq::graphics::NativeWindowHandle window;
	aq::platform::WindowDesc         desc;
	if (!platform_->CreateMainWindow(desc, window))
	{
		aq::StartupMark("[ios] CreateMainWindow FAILED");
		return YES;
	}

	// 画面サイズは OS が決めるので、desc ではなく実際に確保できた寸法を使う(§3.5)。
	const uint32_t width  = static_cast<uint32_t>(platform_->GetDrawableWidth());
	const uint32_t height = static_cast<uint32_t>(platform_->GetDrawableHeight());

	aq::graphics::GraphicsDevice::Create<aq::graphics::MetalGraphicsDeviceImpl>();
	if (!aq::graphics::GraphicsDevice::Get().Initialize(window, width, height))
	{
		aq::StartupMark("[ios] GraphicsDevice::Initialize FAILED");
		aq::graphics::GraphicsDevice::Release();
		return YES;
	}
	aq::StartupMark("[ios] graphics device ok");

	aq::graphics::GraphicsDevice::Get().SetupRenderContext(renderContext_);
	aq::graphics::GraphicsDevice::Get().SetupDefaultRenderState(renderContext_);
	graphicsReady_ = true;

	// フレーム駆動をプラットフォームへ委譲する。P1 の足場も Engine と同じ経路を通し、
	// P2 で本物(Engine::RunGame)へ差し替わる機構をそのまま検証する。
	// RunFrameLoop は CADisplayLink を張って即 return するので、この後 return YES まで進む。
	aq::platform::PlatformiOS* platform = platform_;
	AqAppDelegate* delegate = self;
	platform->RunFrameLoop([delegate, width, height]
		{
			[delegate presentClearFrame:width height:height];
		});

	return YES;
}


// グラフィクスデバイスだけを立てた状態で、クリア色を 1 フレーム提示する。
//
// 検証対象は「UIKit → CAMetalLayer → Metal のドローアブル/提示」と
// 「CADisplayLink によるフレーム駆動」の 2 点だけ(設計書 §9 の P1)。
- (void)presentClearFrame:(uint32_t)width height:(uint32_t)height
{
	if (!graphicsReady_)
	{
		return;
	}

	// 見て分かる色にする(黒だと「何も出ていない」と区別が付かない)。
	// AndroidMain.cpp の P1 版と同じ色にして、見比べられるようにしてある。
	float clearColor[4] = { 0.10f, 0.35f, 0.60f, 1.0f };

	aq::graphics::IRenderTarget& mainRT =
		aq::graphics::GraphicsDevice::Get().GetMainRenderTarget(0);

	renderContext_.OMSetRenderTargets(1, &mainRT);
	renderContext_.RSSetViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
	renderContext_.ClearRenderTargetView(0, clearColor);

	aq::graphics::GraphicsDevice::Get().CopyToBackBuffer(mainRT);
	aq::graphics::GraphicsDevice::Get().Present();

	// 提示が回り始めたことがログで分かるように、最初の数フレームだけ印を出す。
	if (presentedFrames_ < 3)
	{
		++presentedFrames_;
		aq::StartupMarkf("[ios] presented frame %u", presentedFrames_);
	}
}


- (void)applicationWillTerminate:(UIApplication*)application
{
	(void)application;

	// まずフレーム駆動を止める。CADisplayLink が生きたままデバイスを壊すと、
	// 次の表示更新で解放済みのデバイスへ描きに行く。
	if (platform_ != nullptr)
	{
		platform_->StopFrameLoop();
	}

	if (graphicsReady_)
	{
		aq::graphics::GraphicsDevice::Get().WaitIdle();
		aq::graphics::GraphicsDevice::Get().Finalize();
		aq::graphics::GraphicsDevice::Release();
		graphicsReady_ = false;
	}

	delete platform_;
	platform_ = nullptr;

	// Engine もプラットフォームも壊れた後に畳む。ここで初めてリーク報告が意味を持つ
	// (4 つのエントリ共通の順序)。
	//
	// **なぜ Engine::Finalize() の直後ではなくここなのか**: UIApplicationMain が
	// 戻ってこないため、main の末尾に相当する場所がこのコールバックしかない。
	// P2 で Engine を起動するようになっても、Engine::Finalize / Engine::Release は
	// このメソッドの中(ShutdownMemory より前)へ置くこと。
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
