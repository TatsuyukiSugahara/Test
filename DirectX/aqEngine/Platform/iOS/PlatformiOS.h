#pragma once
// iOS(UIKit)向けプラットフォーム実装。
// 他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_IOS)
#include "Platform/IPlatform.h"

namespace aq
{
	namespace platform
	{
		// UIKit オブジェクト群(UIWindow / UIViewController / AqMetalView / CAMetalLayer)。
		// Objective-C 型をヘッダへ漏らさないため、定義は PlatformiOS.mm 側に置く
		// (PlatformMac.h の MacWindowObjects と同じ作法)。
		struct iOSWindowObjects;


		// UIWindow + CAMetalLayer をホストする UIView の上で IPlatform を実装する。
		//
		// Win32 / Mac と決定的に違うのは「メインループを自分で回せない」こと。
		// UIApplicationMain は戻ってこず、run loop は UIKit が所有するため、
		// `while (PumpEvents()) { ... }` の形が成立しない。フレーム駆動は
		// CADisplayLink のコールバックから 1 フレーム分だけ進める形になり、
		// そのために IPlatform へ RunFrameLoop を足して Engine のループを
		// プラットフォーム側へ委譲する(P1。設計書/iOS移植設計.md §3.3)。
		//
		// Android と違いウィンドウは自前で生成できる(OS からの到着を待たない)が、
		// 生成できるのは UIApplicationDelegate のコールバック以降に限られる。
		//
		// NativeWindowHandle には CAMetalLayer* を格納する(Mac と同じ。Metal の
		// 提示先がレイヤのため)。
		class PlatformiOS : public IPlatform
		{
		// ── メンバ変数 ──
		private:
			/** UIKit オブジェクト群(CreateMainWindow で生成し、デストラクタで解放) */
			iOSWindowObjects* objects_;


		// ── メンバ関数 ──
		public:
			PlatformiOS();
			~PlatformiOS() override;


		public:
			bool        CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out) override;
			bool        PumpEvents() override;
			const char* GetContentRoot() override;
			const char* GetUserDataDirectory() override;

			// IsRenderable は override しない(既定の true のまま)。
			// TODO(P5): バックグラウンド遷移(applicationDidEnterBackground)の間は
			// ドロウアブルを取りに行ってはいけないので、そこで false を返すようにする。
		};
	}
}
#endif // AQ_PLATFORM_IOS
