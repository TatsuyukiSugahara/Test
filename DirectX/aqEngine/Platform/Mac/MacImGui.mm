#include "aq.h"
// macOS 以外、および ImGui 無効構成では空 TU にする
// (Win32 は imgui_impl_win32 が同じ責務を持つ)。
#if defined(AQ_PLATFORM_MAC) && defined(AQ_IMGUI)
#include "Platform/Mac/MacImGui.h"
#import <Cocoa/Cocoa.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_osx.h>

// 本 TU は手動参照カウント(MRR)前提。CMake は -fobjc-arc を渡していない。
// ARC が要るのは公式の imgui_impl_osx.mm だけで、CMake がそのファイルにだけ付けている。
// 本 TU が保持する ObjC のオブジェクトは Init で受けたビューだけで、retain / release を手で対にする。
#if __has_feature(objc_arc)
#error "MacImGui.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif

// ─────────────────────────────────────────────────────────────────────────
// 公式バックエンド imgui_impl_osx への接続層 (E1b-2。設計書「エディタ全体設計」4.5 節)。
// 入力は公式が Init で張るイベントモニタ、フォーカスは公式が購読するアプリのアクティブ化通知に任せる。
// ゲーム入力との排他は AppHost::Update の WantCapture* → Suppress* で効く(ここでは何もしない)。
// ─────────────────────────────────────────────────────────────────────────

namespace aq
{
	namespace platform
	{
		namespace MacImGui
		{
			namespace
			{
				/** 公式の NewFrame へ毎フレーム渡すビュー(Init で retain、Shutdown で release) */
				NSView* g_view = nil;
			}


			bool Init(NSView* view)
			{
				EngineAssertMsg(ImGui::GetCurrentContext() != nullptr, "ImGui のコンテキストが未生成です");
				if (ImGui::GetCurrentContext() == nullptr || view == nil) {
					return false;
				}

				if (!ImGui_ImplOSX_Init(view)) {
					EngineAssertMsg(false, "ImGui OSX バックエンド初期化失敗");
					return false;
				}
				g_view = [view retain];
				return true;
			}


			void Shutdown()
			{
				if (g_view == nil) return;
				ImGui_ImplOSX_Shutdown();
				[g_view release];
				g_view = nil;
			}


			void NewFrame()
			{
				if (g_view == nil) return;

				// DisplaySize(view.bounds)と DeltaTime(mach 時間)は公式の値をそのまま使う。
				ImGui_ImplOSX_NewFrame(g_view);

				// 公式は DisplayFramebufferScale に backingScaleFactor(Retina では 2)を入れるが、
				// PlatformMac は contentsScale = 1 に固定して drawable をポイント単位にしている
				// (Mac移植設計 §8-13)。2 のままだと公式 metal / vulkan がビューポートとシザーを
				// 2 倍で計算してずれるので、(1,1) に戻す。
				ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
			}
		}
	}
}
#endif // AQ_PLATFORM_MAC && AQ_IMGUI
