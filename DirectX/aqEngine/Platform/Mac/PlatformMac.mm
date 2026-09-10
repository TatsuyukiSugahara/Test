#include "aq.h"
// macOS(Cocoa)専用実装。他構成では本体をガードして空 TU にする
// (Win32 は PlatformWin32.cpp、UWP は PlatformUWP.cpp が代替)。
#if defined(AQ_PLATFORM_MAC)
#include "Platform/Mac/PlatformMac.h"
#include "HID/Mac/CocoaInputSink.h"
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <cstdlib>

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
// ARC を有効にすると下の release 呼び出しがコンパイルエラーになるため、早期に落とす。
#if __has_feature(objc_arc)
#error "PlatformMac.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


// 起動診断ログ。Mac では起動計測(StartupMark)に流して経過時間付きで記録する。
// Win32/UWP と同じく「プラットフォーム実装が 1 つだけ定義を持つ」構造(aq.h で宣言)。
namespace aq { void StartupLog(const char* msg) { StartupMark(msg); } }


// ウィンドウの閉じるボタンを終了要求へ変換するデリゲート。
// NSWindow の delegate は weak 参照なので、寿命は PlatformMac 側で持つ。
@interface AqWindowDelegate : NSObject <NSWindowDelegate>
{
	aq::platform::PlatformMac* platform_;
}
- (instancetype)initWithPlatform:(aq::platform::PlatformMac*)platform;
@end


// CAMetalLayer をホストするビュー。
// 表示スケールの変化(別ディスプレイへの移動)とリサイズに追従して
// contentsScale / drawableSize を更新するためだけに NSView を派生している。
@interface AqMetalView : NSView
@end


namespace
{
	// レイヤのドロウアブルサイズをビューの論理サイズ(ポイント)に合わせる。
	//
	// HiDPI の扱い(設計書 §8-13 の未決事項。P2 で下記に決定):
	//   Retina では backingScaleFactor = 2 なので、素直に物理ピクセルへ合わせると
	//   drawableSize が 2560x1440 になる。一方 Engine のレンダーターゲット・深度は
	//   InitializeParameter の 1280x720 のままなので、スワップチェーン(サーフェス
	//   capabilities 由来 = 2560x1440)と深度アタッチメント(1280x720)が同じ
	//   vkCmdBeginRendering に並び、VUID-VkRenderingInfo-pNext-06079/06080 に掛かる。
	//
	//   P2 では **contentsScale = 1 に固定してドロウアブルを論理サイズと一致させる**。
	//   Windows と同じ 1280x720 で 1 枚だけ描き、Retina への引き伸ばしは Core Animation
	//   に任せる(その分ぼやける)。Engine 側に解像度の概念を増やさずに済み、
	//   「Windows Vulkan 構成と同じ見た目」という P3 の評価基準とも噛み合うため。
	//
	//   ネイティブ解像度で描く案(screenWidth/Height をドロウアブル側に合わせ、
	//   renderWidth/Height は据え置いて最終パスで拡大)は、UI のヒットテスト座標系と
	//   ImGui の DisplayFramebufferScale まで巻き込むので P4 以降で扱う。
	void UpdateLayerBacking(NSView* view)
	{
		id layerObject = [view layer];
		if (![layerObject isKindOfClass:[CAMetalLayer class]])
		{
			return;
		}
		CAMetalLayer* layer = layerObject;

		// レイヤホスティングでは contentsScale を自分で設定する必要がある
		// (Core Animation Programming Guide「Setting Up Layer Objects」)。
		// 1 を入れると、以降 convertRectToBacking とは無関係に 1 ポイント = 1 ピクセルになる。
		[layer setContentsScale:1.0];

		const NSSize bounds = [view bounds].size;
		if (bounds.width > 0.0 && bounds.height > 0.0)
		{
			[layer setDrawableSize:CGSizeMake(bounds.width, bounds.height)];
		}
	}


	// イベントの位置をビュー座標(左上原点)へ直して入力シンクへ渡す。
	// Cocoa は左下原点なので Y を反転する。§8-13 の決定で contentsScale = 1 に
	// してあるため、ポイント座標と描画ピクセルは 1:1 で倍率補正は要らない。
	void PushMousePosition(NSEvent* event, NSView* view, float deltaX, float deltaY)
	{
		const NSPoint inView = [view convertPoint:[event locationInWindow] fromView:nil];
		const CGFloat height = [view bounds].size.height;
		aq::hid::CocoaInputSink::Get().OnMouseMove(
			static_cast<float>(inView.x),
			static_cast<float>(height - inView.y),
			deltaX, deltaY);
	}


	// NSEvent を CocoaInputSink へ振り分ける(設計書/Mac移植設計.md §3.2)。
	// ここが「Cocoa の型を扱う最後の場所」で、シンクから先には NSEvent / NSView は出ない。
	void DispatchInputEvent(NSEvent* event, NSView* view)
	{
		if (event == nil || view == nil)
		{
			return;
		}
		// 別ウィンドウ(将来のパネル等)のイベントは拾わない。
		// keyDown 等は window が nil になることがないが、念のため両方を見る。
		NSWindow* eventWindow = [event window];
		if (eventWindow != nil && eventWindow != [view window])
		{
			return;
		}

		aq::hid::CocoaInputSink& sink = aq::hid::CocoaInputSink::Get();

		switch ([event type])
		{
		case NSEventTypeKeyDown:
			// isARepeat のリピートも「押されている」ことに変わりはないのでそのまま通す。
			sink.OnKey([event keyCode], true);
			break;

		case NSEventTypeKeyUp:
			sink.OnKey([event keyCode], false);
			break;

		case NSEventTypeFlagsChanged:
			sink.OnModifierFlagsChanged(([event modifierFlags] & NSEventModifierFlagCommand) != 0);
			break;

		case NSEventTypeMouseMoved:
		case NSEventTypeLeftMouseDragged:
		case NSEventTypeRightMouseDragged:
		case NSEventTypeOtherMouseDragged:
			PushMousePosition(event, view,
			                  static_cast<float>([event deltaX]),
			                  static_cast<float>([event deltaY]));
			break;

		case NSEventTypeLeftMouseDown:
		case NSEventTypeLeftMouseUp:
		case NSEventTypeRightMouseDown:
		case NSEventTypeRightMouseUp:
		case NSEventTypeOtherMouseDown:
		case NSEventTypeOtherMouseUp:
		{
			const NSEventType type = [event type];
			const bool pressed = (type == NSEventTypeLeftMouseDown)
			                  || (type == NSEventTypeRightMouseDown)
			                  || (type == NSEventTypeOtherMouseDown);
			// buttonNumber は左=0 / 右=1 / 中=2 で、DirectInput の rgbButtons と同じ並び。
			sink.OnMouseButton(static_cast<int32_t>([event buttonNumber]), pressed);
			// クリックだけでカーソルが動かない場合もあるので位置も更新しておく。
			PushMousePosition(event, view, 0.0f, 0.0f);
			break;
		}

		case NSEventTypeScrollWheel:
			sink.OnScrollWheel(static_cast<float>([event scrollingDeltaY]));
			break;

		default:
			break;
		}
	}
}


@implementation AqWindowDelegate

- (instancetype)initWithPlatform:(aq::platform::PlatformMac*)platform
{
	self = [super init];
	if (self != nil)
	{
		platform_ = platform;
	}
	return self;
}


// 非アクティブになったキー/ボタンを持ち越さない。macOS は Command 押下中の keyUp を
// 配送しないなど、押しっぱなしが残りやすい(CocoaInputSink 側でも同じ対策を入れている)。
- (void)windowDidResignKey:(NSNotification*)notification
{
	(void)notification;
	aq::hid::CocoaInputSink::Get().OnFocusLost();
}


- (BOOL)windowShouldClose:(NSWindow*)sender
{
	(void)sender;
	if (platform_ != nullptr)
	{
		platform_->RequestExit();
	}
	// releasedWhenClosed = NO にしてあるので、閉じてもウィンドウは解放されない。
	// 次の PumpEvents が false を返し、RunGame → Finalize へ抜ける。
	return YES;
}

@end


@implementation AqMetalView

- (void)viewDidChangeBackingProperties
{
	[super viewDidChangeBackingProperties];
	UpdateLayerBacking(self);
}


- (void)setFrameSize:(NSSize)newSize
{
	[super setFrameSize:newSize];
	UpdateLayerBacking(self);
}


// レイヤホスティングなのでビュー自身は描かない(描画は Core Animation 側)。
- (BOOL)isOpaque
{
	return YES;
}


// P4 でキーイベントを受けるためにファーストレスポンダになれるようにしておく。
- (BOOL)acceptsFirstResponder
{
	return YES;
}

@end


namespace aq
{
	namespace platform
	{
		// Cocoa オブジェクト群。MRR のため alloc / retain した分をデストラクタで release する。
		struct MacWindowObjects
		{
			NSWindow*          window         = nil;
			AqMetalView*       view           = nil;
			CAMetalLayer*      layer          = nil;
			AqWindowDelegate*  windowDelegate = nil;
		};


		PlatformMac::PlatformMac()
			: objects_(nullptr)
			, contentRoot_()
			, contentRootResolved_(false)
			, exitRequested_(false)
		{
		}


		PlatformMac::~PlatformMac()
		{
			if (objects_ == nullptr)
			{
				return;
			}

			@autoreleasepool
			{
				if (objects_->window != nil)
				{
					// delegate は weak 参照。先に外してからデリゲート本体を解放する。
					[objects_->window setDelegate:nil];
					[objects_->window close];
				}
				[objects_->windowDelegate release];
				[objects_->layer release];
				[objects_->view release];
				[objects_->window release];
			}

			delete objects_;
			objects_ = nullptr;
		}


		bool PlatformMac::CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out)
		{
			EngineAssert(desc.width);
			EngineAssert(desc.height);
			// NSWindow の生成前に NSApplication が必要。生成は MacMain.mm が行う。
			EngineAssertMsg(NSApp != nil, "NSApplication が未生成です");

			@autoreleasepool
			{
				const NSRect contentRect = NSMakeRect(
					0.0, 0.0,
					static_cast<CGFloat>(desc.width), static_cast<CGFloat>(desc.height));
				const NSWindowStyleMask styleMask =
					NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
					NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

				NSWindow* window = [[NSWindow alloc] initWithContentRect:contentRect
				                                              styleMask:styleMask
				                                                backing:NSBackingStoreBuffered
				                                                  defer:NO];
				if (window == nil)
				{
					return false;
				}

				// 既定(YES)のままだと閉じるボタンでウィンドウが解放され、こちらの
				// 参照が宙に浮く。解放は PlatformMac のデストラクタで行う。
				[window setReleasedWhenClosed:NO];
				[window setTitle:[NSString stringWithUTF8String:(desc.title != nullptr ? desc.title : "Application")]];
				[window center];
				// マウス移動イベントは既定で配送されない。P4 の CocoaMouseBackend で要る。
				[window setAcceptsMouseMovedEvents:YES];

				AqWindowDelegate* windowDelegate = [[AqWindowDelegate alloc] initWithPlatform:this];
				[window setDelegate:windowDelegate];

				// レイヤホスティングビューを作る。CAMetalLayer を自前で管理するため
				// 「layer を先に設定 → wantsLayer = YES」の順にする(NSView.wantsLayer の
				// ドキュメントに「順序が重要」と明記されている)。
				AqMetalView* view = [[AqMetalView alloc] initWithFrame:contentRect];
				CAMetalLayer* layer = [CAMetalLayer layer];
				// layer.device / pixelFormat は設定しない。MoltenVK が
				// vkCreateMetalSurfaceEXT の中で自分で設定する(実機確認済み)。
				[view setLayer:layer];
				[view setWantsLayer:YES];

				[window setContentView:view];
				[window makeFirstResponder:view];
				// HiDPI の扱いは UpdateLayerBacking のコメントを参照(設計書 §8-13。
				// P2 では contentsScale = 1 に固定してドロウアブルを 1280x720 に揃える)。
				UpdateLayerBacking(view);

				[window makeKeyAndOrderFront:nil];
				// アプリを前面に出す。`NSApp run` を使わず自前でループを回しているため、
				// これが無いとアプリがアクティブにならず、**ウィンドウがキーウィンドウに
				// ならないのでキーボードイベントがキューに入らない**(P2.5 で発覚)。
				if (@available(macOS 14.0, *))
				{
					[NSApp activate];
				}
				else
				{
					[NSApp activateIgnoringOtherApps:YES];
				}

				objects_ = new MacWindowObjects();
				objects_->window         = window;
				objects_->view           = view;
				objects_->layer          = [layer retain];   // [CAMetalLayer layer] は autorelease
				objects_->windowDelegate = windowDelegate;

				// Vulkan(MoltenVK)の VK_EXT_metal_surface が要求するのは CAMetalLayer*。
				// Win32 の HWND / UWP の CoreWindow に相当する位置づけ(設計書 §2.2)。
				out.handle = static_cast<void*>(objects_->layer);
			}
			return out.handle != nullptr;
		}


		bool PlatformMac::PumpEvents()
		{
			@autoreleasepool
			{
				for (;;)
				{
					// untilDate:nil は distantPast と同義で、イベントが無ければ即 nil が返る
					// (NSApplication.nextEventMatchingMask のドキュメント)。
					NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
					                                    untilDate:nil
					                                       inMode:NSDefaultRunLoopMode
					                                      dequeue:YES];
					if (event == nil)
					{
						break;
					}

					// 入力シンクへ転送してから NSApplication へ流す。
					// TODO(P4): imgui_impl_osx は NSView にイベントモニタを張るため、
					// ここの sendEvent と二重処理になりうる(設計書 §8-5)。順序は P4 で再確認する。
					DispatchInputEvent(event, objects_ != nullptr ? objects_->view : nil);
					[NSApp sendEvent:event];
				}
			}
			return !exitRequested_;
		}


		const char* PlatformMac::GetContentRoot()
		{
			if (!contentRootResolved_)
			{
				contentRootResolved_ = true;

				@autoreleasepool
				{
					// 1) 環境変数。ソースツリー外の Assets を差し替えるための逃げ道。
					if (const char* env = std::getenv("AQ_CONTENT_ROOT"))
					{
						if (env[0] != '\0')
						{
							contentRoot_ = env;
						}
					}

					// 2) .app バンドルなら Contents/Resources。
					//    コマンドラインから直接 exe を起動した場合、mainBundle は実行ファイルの
					//    ディレクトリを指す「バンドルでないバンドル」になるため、拡張子で
					//    本物の .app だけを採る。
					// TODO(Mac実機): 要確認 — 非バンドル実行時の mainBundle の bundlePath が
					// 何を返すかは Apple の公式ドキュメントに記載が無い。拡張子判定で弾ける
					// ことを実機で確認する(弾けないと FindProjectRoot へ落ちなくなる)。
					if (contentRoot_.empty())
					{
						NSBundle* bundle = [NSBundle mainBundle];
						NSString* bundlePath = [bundle bundlePath];
						if ([[bundlePath pathExtension] isEqualToString:@"app"])
						{
							NSString* resourcePath = [bundle resourcePath];
							if (resourcePath != nil)
							{
								contentRoot_ = [resourcePath UTF8String];
							}
						}
					}
				}
			}

			// 3) 空なら nullptr。Win32 と同じく Resource 側の FindProjectRoot 探索へ委ねる。
			return contentRoot_.empty() ? nullptr : contentRoot_.c_str();
		}


		void* PlatformMac::GetNSView() const
		{
			return objects_ != nullptr ? static_cast<void*>(objects_->view) : nullptr;
		}
	}
}
#endif // AQ_PLATFORM_MAC
