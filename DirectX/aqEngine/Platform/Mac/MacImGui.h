#pragma once
// macOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC) && defined(AQ_IMGUI)

#ifdef __OBJC__
@class NSEvent;
@class NSView;
#endif

namespace aq
{
	namespace platform
	{
		/**
		 * ImGui の Mac プラットフォームバックエンド(imgui_impl_win32 相当)。
		 *
		 * 描画は既存の VulkanImGui が持つので、ここが見るのは入力・カーソル形状・
		 * クリップボードと、フレーム頭の DisplaySize / DeltaTime だけ。
		 * imgui_impl_osx は同梱しない(設計書/Mac移植設計.md P4b)。NSEvent の入り口は
		 * PlatformMac::PumpEvents の 1 本のままで、HandleEvent はそこから呼ばれる。
		 */
		namespace MacImGui
		{
			/** バックエンドの登録(ImGui のコンテキスト生成後に呼ぶ) */
			bool Init();

			/** バックエンドの登録解除(ImGui のコンテキスト破棄前に呼ぶ) */
			void Shutdown();

			/** フレーム頭の状態更新(ImGui::NewFrame の前に呼ぶ) */
			void NewFrame();

			/**
			 * ウィンドウのキーフォーカスが変化した(NSWindowDelegate から呼ぶ)。
			 *
			 * 失った側だけ通知するのでは足りない。ImGui の io.AppFocusLost は**立ちっぱなしの
			 * フラグ**で、真の間は毎フレーム ClearInputKeys / ClearInputMouse が走るため
			 * (imgui.cpp の UpdateInputEvents 末尾)、復帰時に true を返さないと以後の入力が
			 * すべて捨てられる。
			 */
			void OnFocusChanged(bool focused);

#ifdef __OBJC__
			// NSEvent を ImGui へ流す。呼び出し側の Application.cpp は素の C++ なので
			// Objective-C 型を使うこの 1 本だけ __OBJC__ で囲う(設計書 §6)。
			void HandleEvent(NSEvent* event, NSView* view);
#endif
		}
	}
}
#endif // AQ_PLATFORM_MAC && AQ_IMGUI
