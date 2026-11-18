#pragma once
// macOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC) && defined(AQ_IMGUI)

#ifdef __OBJC__
@class NSView;
#endif


namespace aq
{
	namespace platform
	{
		/**
		 * ImGui の Mac プラットフォームバックエンド(公式 imgui_impl_osx)への接続層(E1b-2)
		 *
		 * 入力(マウス・キー・文字・IME)・フォーカス・カーソル形状・クリップボードは公式 osx が持つ。
		 * 公式は Init で渡したビューにイベントモニタと KeyEventResponder を張り、
		 * PlatformMac::PumpEvents の [NSApp sendEvent:] を通るイベントを自分で拾う。
		 * ここが受け持つのは Init / Shutdown の中継と、NewFrame での DisplayFramebufferScale の補正だけ。
		 * 詳細は 設計書/エディタ全体設計.md 4.5 節。
		 */
		namespace MacImGui
		{
#ifdef __OBJC__
			/**
			 * バックエンドの登録(ImGui のコンテキスト生成後に呼ぶ)
			 *
			 * 引数に Objective-C 型を取るので __OBJC__ の中に置く(呼び出し元の PlatformMac.mm は ObjC++)。
			 * @param view 入力を拾うビュー(PlatformMac の AqMetalView)。Shutdown まで保持する
			 */
			bool Init(NSView* view);
#endif

			/** バックエンドの登録解除(ImGui のコンテキスト破棄前に呼ぶ) */
			void Shutdown();

			/** フレーム頭の状態更新(ImGui::NewFrame の前に呼ぶ) */
			void NewFrame();
		}
	}
}
#endif // AQ_PLATFORM_MAC && AQ_IMGUI
