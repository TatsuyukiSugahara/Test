#include "aq.h"
// iOS(UIKit)専用実装。他構成では本体をガードして空 TU にする
// (Win32 は PlatformWin32.cpp、UWP は PlatformUWP.cpp、macOS は PlatformMac.mm が代替)。
#if defined(AQ_PLATFORM_IOS)
#include "Platform/iOS/PlatformiOS.h"
#include "HID/iOS/iOSInputSink.h"
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
// ARC を有効にすると release 呼び出しがコンパイルエラーになるため、早期に落とす。
#if __has_feature(objc_arc)
#error "PlatformiOS.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


// CAMetalLayer をホストするビュー。
//
// Mac(NSView)は「レイヤを自分で作って setLayer: + wantsLayer = YES」だが、
// UIKit のビューは必ずレイヤに裏打ちされていて差し替えられない。代わりに
// **+layerClass を override して、ビューが自分のレイヤとして CAMetalLayer を作る**
// のが iOS の作法(設計書/iOS移植設計.md §4.1 の表)。
//
// タッチの入口も兼ねる。touches*:withEvent: を iOSInputSink へ流し、
// iOSTouchBackend がそれを読む(設計書/iOS移植設計.md §5.2)。
@interface AqMetalView : UIView
@end


// CADisplayLink のターゲット。セレクタの送り先は Objective-C オブジェクトでないと
// いけないので、PlatformiOS への転送だけを行う小さなクラスを挟む
// (PlatformMac.mm の AqWindowDelegate と同じ作法)。
// CADisplayLink はターゲットを retain するため、循環参照にならないよう
// こちら側は PlatformiOS を所有しない(生ポインタで持つ)。
@interface AqDisplayLinkTarget : NSObject
{
	aq::platform::PlatformiOS* platform_;
}
- (instancetype)initWithPlatform:(aq::platform::PlatformiOS*)platform;
- (void)onDisplayLinkFrame:(CADisplayLink*)displayLink;
@end


namespace
{
	// レイヤのドロウアブルサイズをビューの論理サイズ(ポイント)へ合わせる。
	//
	// **contentsScale は 1.0 に固定する**(設計書/iOS移植設計.md §3.5 / §0.5-4)。
	// Mac と同じ判断で、理由も同じ:
	//   Retina では nativeScale が 2〜3 あるので、素直に物理ピクセルへ合わせると
	//   ドロウアブルだけが 2〜3 倍になり、Engine のレンダーターゲット・深度
	//   (InitializeParameter の値)と寸法が食い違う。Engine 側に解像度の概念を
	//   増やさずに済ませるため、1 ポイント = 1 ピクセルで 1 枚だけ描き、
	//   画面への引き伸ばしは Core Animation に任せる(その分ぼやける)。
	//   ImGui の DisplayFramebufferScale = (1,1) 前提もこれで保たれる。
	// ネイティブ解像度で描く案は P6 の性能調整項目。
	void UpdateLayerBacking(UIView* view)
	{
		id layerObject = [view layer];
		if (![layerObject isKindOfClass:[CAMetalLayer class]])
		{
			return;
		}
		CAMetalLayer* layer = layerObject;

		[layer setContentsScale:1.0];

		const CGSize bounds = [view bounds].size;
		if (bounds.width > 0.0 && bounds.height > 0.0)
		{
			[layer setDrawableSize:CGSizeMake(bounds.width, bounds.height)];
		}
	}
}


@implementation AqMetalView

// ビューのレイヤを CAMetalLayer にする。UIView は初期化時にこのクラスのレイヤを
// 生成するので、以降 [view layer] は常に CAMetalLayer を返す。
+ (Class)layerClass
{
	return [CAMetalLayer class];
}


// 初期レイアウトとサイズ変更に追従する。向きは横向き固定(Info.plist)なので
// 実際に寸法が変わるのは起動直後の 1 回だけ。
// TODO(P5): 回転やスプリットビューで寸法が変わる構成を許すなら、Metal 側の
// リサイズ経路(設計書 §4.5)を用意してから Engine へ変化を通知する必要がある。
- (void)layoutSubviews
{
	[super layoutSubviews];
	UpdateLayerBacking(self);
}


// レイヤホスティングなのでビュー自身は描かない(描画は Metal 側)。
- (BOOL)isOpaque
{
	return YES;
}


// ── タッチ ──────────────────────────────────────────────
//
// 座標は [touch locationInView:self] で取る。**UIKit のビュー座標は左上原点**なので、
// TouchPoint の契約(クライアント領域の左上原点・ピクセル)にそのまま合う。
// Mac(PlatformMac.mm)は NSView が左下原点で Y を反転していたが、iOS では反転しない。
//
// 単位はポイントだが、UpdateLayerBacking が contentsScale = 1.0 に固定していて
// ドロウアブルもビューの論理サイズと同寸(設計書 §3.5)なので、
// **1 ポイント = 1 ピクセル**。スケール補正も要らない。
//
// UITouch* は retain せず、アドレスを同一性の鍵として渡すだけ
// (Apple が retain を明示的に禁止している。iOSInputSink.h の touchKey の説明を参照)。

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
	(void)event;
	for (UITouch* touch in touches)
	{
		const CGPoint pos = [touch locationInView:self];
		aq::hid::iOSInputSink::Get().OnTouchBegan(static_cast<const void*>(touch),
		                                          static_cast<float>(pos.x),
		                                          static_cast<float>(pos.y));
	}
}


- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
	(void)event;
	for (UITouch* touch in touches)
	{
		const CGPoint pos = [touch locationInView:self];
		aq::hid::iOSInputSink::Get().OnTouchMoved(static_cast<const void*>(touch),
		                                          static_cast<float>(pos.x),
		                                          static_cast<float>(pos.y));
	}
}


- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
	(void)event;
	for (UITouch* touch in touches)
	{
		const CGPoint pos = [touch locationInView:self];
		aq::hid::iOSInputSink::Get().OnTouchEnded(static_cast<const void*>(touch),
		                                          static_cast<float>(pos.x),
		                                          static_cast<float>(pos.y));
	}
}


// ジェスチャが OS 側に奪われた(ホームへ戻る・通知センターを引き下ろす等)。
// **これを実装しないと ended が来ないまま指が張り付く**(Android の ACTION_CANCEL と同じ)。
// どの指が取り消されたかに関わらず全点を解放する。
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
	(void)touches;
	(void)event;
	aq::hid::iOSInputSink::Get().OnTouchCancelled();
}

@end


@implementation AqDisplayLinkTarget

- (instancetype)initWithPlatform:(aq::platform::PlatformiOS*)platform
{
	self = [super init];
	if (self != nil)
	{
		platform_ = platform;
	}
	return self;
}


- (void)onDisplayLinkFrame:(CADisplayLink*)displayLink
{
	(void)displayLink;
	if (platform_ != nullptr)
	{
		platform_->OnDisplayLinkFrame();
	}
}

@end


namespace aq
{
	namespace platform
	{
		// UIKit オブジェクト群。MRR のため alloc / retain した分をデストラクタで release する。
		struct iOSWindowObjects
		{
			UIWindow*            window             = nil;
			UIViewController*    rootViewController = nil;
			AqMetalView*         view               = nil;
			CAMetalLayer*        layer              = nil;
			CADisplayLink*       displayLink        = nil;
			AqDisplayLinkTarget* displayLinkTarget  = nil;
		};


		/**
		 * iOS のプラットフォーム実装
		 */
		PlatformiOS::PlatformiOS()
			: objects_(nullptr)
			, frameCallback_()
			, contentRoot_()
			, contentRootResolved_(false)
			, userDataDirectory_()
			, userDataDirectoryResolved_(false)
			, drawableWidth_(0)
			, drawableHeight_(0)
			, exitRequested_(false)
			, renderable_(true)
		{
		}


		PlatformiOS::~PlatformiOS()
		{
			StopFrameLoop();

			if (objects_ == nullptr)
			{
				return;
			}

			@autoreleasepool
			{
				if (objects_->window != nil)
				{
					[objects_->window setHidden:YES];
					[objects_->window setRootViewController:nil];
				}
				[objects_->layer release];
				[objects_->view release];
				[objects_->rootViewController release];
				[objects_->window release];
			}

			delete objects_;
			objects_ = nullptr;
		}


		bool PlatformiOS::CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out)
		{
			// 要求サイズは使わない。iOS ではウィンドウの寸法を OS が決めるので、
			// UIScreen / UIWindow から取った実寸を GetDrawableWidth/Height で返す
			// (設計書/iOS移植設計.md §3.5)。タイトルバーも無いので desc.title も使わない。
			(void)desc;

			// 2 回呼ばれても同じレイヤを返す。P1 の足場は「サイズを知るために先に呼ぶ →
			// 後段が改めて呼ぶ」使い方をするため(PlatformAndroid::CreateMainWindow と同じ配慮)。
			if (objects_ != nullptr && objects_->layer != nil)
			{
				out.handle = static_cast<void*>(objects_->layer);
				return true;
			}

			@autoreleasepool
			{
				const CGRect screenBounds = [[UIScreen mainScreen] bounds];

				AqMetalView* view = [[AqMetalView alloc] initWithFrame:screenBounds];
				if (view == nil)
				{
					return false;
				}
				[view setAutoresizingMask:(UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight)];

				// UIView の既定は「同時に 1 本の指しか配送しない」。仮想パッドは
				// スティックとボタンを同時に押すので、複数タッチを明示的に有効にする
				// (これを忘れると 2 本目以降の touches* が一切来ない)。
				[view setMultipleTouchEnabled:YES];

				// UIWindow には必ずルート View Controller が要る(無いと実行時に警告が出て
				// 画面が出ない)。ゲームは UIKit のビュー階層を使わないので、素の
				// UIViewController にこちらのビューを持たせるだけにする。
				UIViewController* rootViewController = [[UIViewController alloc] init];
				[rootViewController setView:view];

				UIWindow* window = [[UIWindow alloc] initWithFrame:screenBounds];
				if (window == nil)
				{
					[rootViewController release];
					[view release];
					return false;
				}
				[window setRootViewController:rootViewController];
				[window makeKeyAndVisible];

				// makeKeyAndVisible の時点ではまだレイアウトが済んでいないことがある。
				// ドロウアブルの寸法をここで確定させたいので、明示的に流す
				// (layoutSubviews → UpdateLayerBacking が走る)。
				[window layoutIfNeeded];
				UpdateLayerBacking(view);

				id layerObject = [view layer];
				if (![layerObject isKindOfClass:[CAMetalLayer class]])
				{
					aq::StartupMark("[ios] view layer is not a CAMetalLayer");
					[window release];
					[rootViewController release];
					[view release];
					return false;
				}
				CAMetalLayer* layer = layerObject;

				const CGSize viewBounds = [view bounds].size;
				drawableWidth_  = static_cast<int32_t>(viewBounds.width);
				drawableHeight_ = static_cast<int32_t>(viewBounds.height);

				// RunFrameLoop が先に呼ばれていると既に確保済み。作り直すと
				// CADisplayLink を取りこぼすので、その場合は同じ器へ入れる。
				if (objects_ == nullptr)
				{
					objects_ = new iOSWindowObjects();
				}
				objects_->window             = window;
				objects_->rootViewController = rootViewController;
				objects_->view               = view;
				objects_->layer              = [layer retain];   // ビューの持ち物なので retain して持つ

				// Metal の提示先は CAMetalLayer。Win32 の HWND / Android の
				// ANativeWindow に相当する位置づけ(設計書 §4.1)。
				out.handle = static_cast<void*>(objects_->layer);

				aq::StartupMarkf("[ios] window ok (%dx%d)", drawableWidth_, drawableHeight_);
			}
			return out.handle != nullptr;
		}


		bool PlatformiOS::PumpEvents()
		{
			// イベントの配送は UIKit の run loop が行うので、ここで汲み出すものは無い。
			// iOS に「閉じるボタン」に相当するアプリ終了の概念も無いため、実質常に true を
			// 返す。IPlatform の契約(終了要求で false)は保つ(設計書 §3.3)。
			return !exitRequested_;
		}


		void PlatformiOS::RunFrameLoop(const std::function<void()>& frame)
		{
			// CADisplayLink のセレクタから呼ぶ時点では呼び出し側のラムダは生きていない。
			// 参照ではなくメンバへコピーして保持する。
			frameCallback_ = frame;

			// ウィンドウが無くてもフレーム駆動自体は張れる(CreateMainWindow の前に
			// 呼ばれても壊れないようにしておく)。
			if (objects_ == nullptr)
			{
				objects_ = new iOSWindowObjects();
			}
			if (objects_->displayLink != nil)
			{
				// 二重に張ると 1 表示更新で 2 回フレームが進む。差し替えは行わない。
				return;
			}

			@autoreleasepool
			{
				AqDisplayLinkTarget* target = [[AqDisplayLinkTarget alloc] initWithPlatform:this];
				CADisplayLink* displayLink =
					[CADisplayLink displayLinkWithTarget:target selector:@selector(onDisplayLinkFrame:)];

				// preferredFramesPerSecond は既定(= ディスプレイ任せ)のままにする。
				// FPS の上限は GameTimer が持っているので、ここで絞ると二重の制限になる
				// (設計書/iOS移植設計.md §3.3)。
				[displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

				objects_->displayLinkTarget = target;                // alloc の +1 をそのまま保持
				objects_->displayLink       = [displayLink retain];  // displayLinkWithTarget: は autorelease
			}

			// ここで return する。以降のフレームは UIKit の run loop から駆動される。
			aq::StartupMark("[ios] CADisplayLink started");
		}


		void PlatformiOS::StopFrameLoop()
		{
			if (objects_ == nullptr || objects_->displayLink == nil)
			{
				return;
			}

			@autoreleasepool
			{
				// invalidate で run loop から外れ、CADisplayLink が持っていた
				// ターゲットへの参照も落ちる。こちらの +1 は別途 release する。
				[objects_->displayLink invalidate];
				[objects_->displayLink release];
				objects_->displayLink = nil;

				[objects_->displayLinkTarget release];
				objects_->displayLinkTarget = nil;
			}

			frameCallback_ = nullptr;
			aq::StartupMark("[ios] CADisplayLink stopped");
		}


		void PlatformiOS::OnDisplayLinkFrame()
		{
			if (frameCallback_)
			{
				frameCallback_();
			}
		}


		void PlatformiOS::OnSuspend()
		{
			// 冪等。二重に通知が来ても、既に止めていれば何もしない
			// (ここを抜けないと下の「フレームを 1 回だけ回す」が余分に走る)。
			if (!renderable_)
			{
				return;
			}

			// **停止の順序がここの肝**(設計書/iOS移植設計.md §3.4)。
			//   1. renderable_ を false にする
			//   2. フレームを 1 回だけ回す
			//   3. CADisplayLink を止める
			//
			// **3 を先にやってはいけない。** リンクを止めると Engine::FrameStep が
			// 呼ばれなくなり、その先頭にある Engine::SyncSoundActivity() も走らない。
			// サウンドの停止/再開は SyncSoundActivity が IsRenderable() の変化を見て
			// 決めているので、走らせないまま背面へ回ると**背面でも BGM が鳴り続ける**。
			//
			// 逆に 2 の 1 回で SyncSoundActivity がサウンドを止めてくれる。そのフレームの
			// 描画のほうは IsRenderable() が既に false なので FrameStep が丸ごと飛ばす
			// (= **GPU は一切触らない**)。バックグラウンドで Metal のコマンドを出すと
			// iOS はアプリを kill するので、この「触らない」が保証されている必要がある。
			renderable_ = false;

			// RunFrameLoop より前に背面へ回るとコールバックがまだ無い。その場合は
			// 回すものが無いだけで、フラグは落ちているので何も壊れない。
			if (frameCallback_)
			{
				frameCallback_();
			}

			// invalidate ではなく paused。復帰時に張り直さずそのまま再開したいので、
			// リンク自体は生かしておく(破棄は終了時の StopFrameLoop の仕事)。
			// まだ張られていなければ何もしない。
			if (objects_ != nullptr && objects_->displayLink != nil)
			{
				[objects_->displayLink setPaused:YES];
			}

			aq::StartupMark("[ios] suspended");
		}


		void PlatformiOS::OnResume()
		{
			// 冪等。止めていないのに再開すると、paused = NO を二重に打つだけでなく
			// サウンドの状態も余計に触ることになる。
			if (renderable_)
			{
				return;
			}

			// 再開はフレームを回す前にフラグを戻すだけでよい。次の表示更新で
			// FrameStep が走り、SyncSoundActivity がサウンドを続きから鳴らす。
			renderable_ = true;

			if (objects_ != nullptr && objects_->displayLink != nil)
			{
				[objects_->displayLink setPaused:NO];
			}

			aq::StartupMark("[ios] resumed");
		}


		const char* PlatformiOS::GetContentRoot()
		{
			if (!contentRootResolved_)
			{
				contentRootResolved_ = true;

				@autoreleasepool
				{
					// iOS のアプリバンドルは Contents/ 階層を持たず、リソースがバンドル直下に
					// 並ぶ(設計書 §2.3 / §7.3)。ただし**バンドル直下をそのまま基点にはできない**。
					//
					// 基点の下には Game/Assets/... が並ぶ(aq::res::BuildAssetPathCandidates が
					// "Assets/..." を <root>/Game/Assets/... へ組むため)が、フラットバンドルでは
					// **実行ファイル自体が <Bundle>/Game** なので、その "Game" と衝突する。
					// バンドル直下は Info.plist / PkgInfo / _CodeSignature / 実行ファイルという
					// OS 側の名前空間でもあるので、こちらの持ち物は Content/ 1 段に隔離する。
					//
					//     <Bundle>/Game                  … 実行ファイル(OS のもの)
					//     <Bundle>/Content/Game/Assets   … アセット(基点は <Bundle>/Content)
					//
					// 投入側は Tools/PackageApp/package_app.cmake。**片方だけ変えないこと。**
					NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
					if (bundlePath != nil)
					{
						contentRoot_ = [bundlePath UTF8String];
						contentRoot_ += "/Content";
					}
				}
			}

			// 用意できなければ nullptr(Mac / Android と同じ流儀)。
			return contentRoot_.empty() ? nullptr : contentRoot_.c_str();
		}


		const char* PlatformiOS::GetUserDataDirectory()
		{
			if (!userDataDirectoryResolved_)
			{
				userDataDirectoryResolved_ = true;

				@autoreleasepool
				{
					// アプリのコンテナ自体が既にアプリ専用なので、アプリ名のサブフォルダは
					// 作らない(IPlatform::GetUserDataDirectory のコメントが iOS を
					// 名指しでそう決めている)。バンドルは read-only なので書き込みはここへ。
					NSArray* paths = NSSearchPathForDirectoriesInDomains(
						NSDocumentDirectory, NSUserDomainMask, YES);
					if ([paths count] > 0)
					{
						NSString* directory = [paths objectAtIndex:0];

						// 無ければ作る(中間ディレクトリも含む)。既存なら成功扱いになる。
						NSError* error = nil;
						const BOOL created = [[NSFileManager defaultManager]
							createDirectoryAtPath:directory
							withIntermediateDirectories:YES
							attributes:nil
							error:&error];
						if (created)
						{
							userDataDirectory_  = [directory UTF8String];
							userDataDirectory_ += "/";
						}
					}
				}
			}

			return userDataDirectory_.empty() ? nullptr : userDataDirectory_.c_str();
		}
	}
}
#endif // AQ_PLATFORM_IOS
