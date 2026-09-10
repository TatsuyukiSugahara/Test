#pragma once
// macOS(Cocoa)向けプラットフォーム実装。
// 他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC)
#include <string>
#include "Platform/IPlatform.h"

namespace aq
{
	namespace platform
	{
		// Cocoa オブジェクト群(NSWindow / NSView / CAMetalLayer / ウィンドウデリゲート)。
		// Objective-C 型をヘッダへ漏らさないため、定義は PlatformMac.mm 側に置く。
		// 参照: 設計書/Mac移植設計.md §10「NSView/CAMetalLayer が Platform/Mac/ の外に現れない」。
		struct MacWindowObjects;


		// NSWindow + レイヤホスティング NSView(CAMetalLayer)の上で IPlatform を実装する。
		// UWP と違いウィンドウは自前で生成し、閉じ要求は NSWindowDelegate 経由で受ける。
		// イベントは nextEventMatchingMask で非ブロッキングにポンプする。
		// NativeWindowHandle には CAMetalLayer* を格納する(Vulkan の
		// VK_EXT_metal_surface が要求するのがレイヤのため)。
		class PlatformMac : public IPlatform
		{
		private:
			/** Cocoa オブジェクト群(CreateMainWindow で生成し、デストラクタで解放) */
			MacWindowObjects* objects_;

			/** アセット基点(.app バンドルから起動したときのみ非空) */
			std::string contentRoot_;
			bool        contentRootResolved_;

			/** 終了要求(ウィンドウの閉じるボタン) */
			bool exitRequested_;

		public:
			PlatformMac();
			~PlatformMac() override;


		public:
			bool CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out) override;
			bool PumpEvents() override;
			const char* GetContentRoot() override;

			// アプリ終了を要求する(ウィンドウデリゲートから呼ぶ)。
			inline void RequestExit() { exitRequested_ = true; }

			// imgui の OSX バックエンド用。実体は NSView*(P4 で ImGui_ImplOSX_Init へ渡す)。
			// Objective-C 型をヘッダに出さないため void* で返す(設計書 §6)。
			void* GetNSView() const;
		};
	}
}
#endif // AQ_PLATFORM_MAC
