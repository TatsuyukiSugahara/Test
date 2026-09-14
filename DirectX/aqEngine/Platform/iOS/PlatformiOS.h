#pragma once
// iOS(UIKit)向けプラットフォーム実装。
// 他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_IOS)
#include <functional>
#include <string>
#include "Platform/IPlatform.h"

namespace aq
{
	namespace platform
	{
		// UIKit オブジェクト群(UIWindow / UIViewController / AqMetalView / CAMetalLayer /
		// CADisplayLink とそのターゲット)。
		// Objective-C 型をヘッダへ漏らさないため、定義は PlatformiOS.mm 側に置く
		// (PlatformMac.h の MacWindowObjects と同じ作法)。
		struct iOSWindowObjects;


		// UIWindow + CAMetalLayer をホストする UIView の上で IPlatform を実装する。
		//
		// Win32 / Mac と決定的に違うのは「メインループを自分で回せない」こと。
		// UIApplicationMain は戻ってこず、run loop は UIKit が所有するため、
		// `while (PumpEvents()) { ... }` の形が成立しない。フレーム駆動は
		// CADisplayLink のコールバックから 1 フレーム分だけ進める形になり、
		// そのために IPlatform へ足した RunFrameLoop を override する
		// (設計書/iOS移植設計.md §3.3)。
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
			/** UIKit オブジェクト群(CreateMainWindow / RunFrameLoop で生成し、デストラクタで解放) */
			iOSWindowObjects* objects_;

			/**
			 * CADisplayLink から呼ぶ 1 フレーム分の処理。
			 * RunFrameLoop() の引数は呼び出し側の寿命なので、ここへコピーして保持する。
			 */
			std::function<void()> frameCallback_;

			/** アセット基点(アプリバンドル直下)。初回要求時に解決してキャッシュする */
			std::string contentRoot_;
			bool        contentRootResolved_;

			/** ユーザーデータの書き込み先(末尾セパレータ付き)。初回要求時に解決してキャッシュする */
			std::string userDataDirectory_;
			bool        userDataDirectoryResolved_;

			/** 実際に確保したドロウアブルの寸法(ポイント = ピクセル。contentsScale は 1 固定) */
			int32_t drawableWidth_;
			int32_t drawableHeight_;

			/** 終了要求。iOS には閉じるボタンが無いので、実際に立つ経路は今のところ無い */
			bool exitRequested_;


		// ── メンバ関数 ──
		public:
			PlatformiOS();
			~PlatformiOS() override;


		public:
			bool        CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out) override;
			bool        PumpEvents() override;
			void        RunFrameLoop(const std::function<void()>& frame) override;
			const char* GetContentRoot() override;
			const char* GetUserDataDirectory() override;

			// IsRenderable は override しない(既定の true のまま)。
			// TODO(P5): バックグラウンド遷移(applicationDidEnterBackground)の間は
			// ドロウアブルを取りに行ってはいけないので、そこで false を返すようにする。


			/**
			 * フレーム駆動関連
			 */
		public:
			/** CADisplayLink を止めて解放する(applicationWillTerminate: から呼ぶ) */
			void StopFrameLoop();

			/** CADisplayLink のコールバックから 1 フレーム進める(AqDisplayLinkTarget 専用) */
			void OnDisplayLinkFrame();


			/**
			 * 画面サイズ
			 *
			 * iOS はウィンドウサイズを OS が決めるので、WindowDesc の値ではなく
			 * CreateMainWindow が確定させた実寸をここから取る(設計書 §3.5)。
			 */
		public:
			/** ドロウアブルの幅(CreateMainWindow 成功後に有効) */
			inline int32_t GetDrawableWidth()  const { return drawableWidth_; }

			/** ドロウアブルの高さ(CreateMainWindow 成功後に有効) */
			inline int32_t GetDrawableHeight() const { return drawableHeight_; }

			/** アプリ終了を要求する */
			inline void RequestExit() { exitRequested_ = true; }
		};
	}
}
#endif // AQ_PLATFORM_IOS
